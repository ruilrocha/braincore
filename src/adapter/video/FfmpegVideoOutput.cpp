#include "FfmpegVideoOutput.h"

#include "../../domain/VideoFrame.h"
#include "../../domain/VideoSegment.h"
#include "../../domain/port/IVideoSource.h"

#include <avcpp/av.h>
#include <avcpp/avutils.h>
#include <avcpp/codec.h>
#include <avcpp/codeccontext.h>
#include <avcpp/dictionary.h>
#include <avcpp/ffmpeg.h>
#include <avcpp/format.h>
#include <avcpp/formatcontext.h>
#include <avcpp/timestamp.h>
#include <avcpp/videorescaler.h>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace audio::adapter::video {

// ── Pimpl ─────────────────────────────────────────────────────────────

struct FfmpegVideoOutput::Impl {
    std::shared_ptr<port::IVideoSource> source;
    std::string output_path;
    int width;
    int height;
    double fps;

    av::FormatContext fmt_ctx;
    av::VideoEncoderContext enc_ctx;
    av::Stream vid_stream;
    bool opened = false;
    bool closed = false;
    int64_t pts = 0;

    // Global time accumulator for exact frame-count matching with audio.
    double audio_time_acc = 0.0;  ///< Total audio time submitted via onBlock.
    int64_t total_frame_acc = 0;  ///< Total frames emitted so far.

    // Black frame re-used for audio-only blocks and fill.
    VideoFrame black_frame;

    Impl(std::shared_ptr<port::IVideoSource> src, std::string path, int w, int h, double f)
        : source(std::move(src)), output_path(std::move(path)), width(w), height(h), fps(f) {
        av::init();
        av::setFFmpegLoggingLevel(AV_LOG_FATAL);

        black_frame = VideoFrame::black(w, h);
    }

    bool open() {
        if (opened)
            return true;

        std::error_code ec;
        fmt_ctx.openOutput(output_path, ec);
        if (ec)
            return false;

        // H.264 encoder.
        av::Codec codec = av::findEncodingCodec(AV_CODEC_ID_H264);

        enc_ctx = av::VideoEncoderContext(codec);
        enc_ctx.setWidth(width);
        enc_ctx.setHeight(height);
        enc_ctx.setPixelFormat(AV_PIX_FMT_YUV420P);
        // Use 1/90000 timebase (standard video timebase = mdhd default).
        // PTS = frame_index * (90000 / fps). For 25fps: PTS = frame_index * 3600.
        // This avoids timebase conversion rounding.
        enc_ctx.setTimeBase(av::Rational{1, 90000});

        av::Dictionary opts;
        opts.set("preset", "fast");
        opts.set("crf", "23");
        opts.set("bf", "0");  // Disable B-frames: no encoder delay, DTS==PTS from frame 0.
        enc_ctx.open(std::move(opts), codec, ec);
        if (ec)
            return false;

        vid_stream = fmt_ctx.addStream(enc_ctx, ec);
        if (ec)
            return false;

        fmt_ctx.writeHeader(ec);
        if (ec)
            return false;

        opened = true;
        return true;
    }

    void encodeFrame(const VideoFrame& frame) {
        if (!opened || closed)
            return;

        std::error_code ec;

        // Build a YUV420P avcpp frame from the domain VideoFrame (always Yuv420pData).
        // The output encoder is configured for AV_PIX_FMT_YUV420P.
        av::VideoFrame yuv(av::PixelFormat(AV_PIX_FMT_YUV420P), frame.width, frame.height);
        yuv.setComplete(true);
        if (!yuv)
            return;

        const int uv_h = frame.height / 2;
        const int uv_w = frame.width / 2;

        if (const auto* yuv_src = std::get_if<Yuv420pData>(&frame.pixels)) {
            for (int row = 0; row < frame.height; ++row) {
                std::memcpy(
                    yuv.raw()->data[0] + row * yuv.raw()->linesize[0],
                    yuv_src->y.data.data() + static_cast<std::size_t>(row) * yuv_src->y.stride,
                    static_cast<std::size_t>(frame.width));
            }
            for (int row = 0; row < uv_h; ++row) {
                std::memcpy(
                    yuv.raw()->data[1] + row * yuv.raw()->linesize[1],
                    yuv_src->u.data.data() + static_cast<std::size_t>(row) * yuv_src->u.stride,
                    static_cast<std::size_t>(uv_w));
                std::memcpy(
                    yuv.raw()->data[2] + row * yuv.raw()->linesize[2],
                    yuv_src->v.data.data() + static_cast<std::size_t>(row) * yuv_src->v.stride,
                    static_cast<std::size_t>(uv_w));
            }
        } else {
            return;  // Unsupported pixel format for encoding.
        }

        const av::Rational tb = enc_ctx.timeBase();
        // PTS = frame_index * (90000 / fps) — exact integer ticks in 90000Hz timebase.
        const int64_t ticks_per_frame = static_cast<int64_t>(std::round(90000.0 / fps));
        yuv.setPts(av::Timestamp{pts * ticks_per_frame, tb});
        ++pts;

        auto pkt = enc_ctx.encode(yuv, ec);
        if (ec || !pkt)
            return;

        // Fix the last-frame stts entry: set duration explicitly in encoder timebase.
        // libx264 may leave packet duration unset; the muxer then computes it from
        // PTS differences for all-but-last, leaving the last frame with duration 0.
        {
            const av::Rational etb = enc_ctx.timeBase();
            pkt.raw()->duration = static_cast<int64_t>(
                std::round(static_cast<double>(etb.getDenominator()) / (fps * etb.getNumerator())));
        }

        pkt.setStreamIndex(vid_stream.index());
        fmt_ctx.writePacket(pkt, ec);
    }
};

// ── Constructor / Destructor ───────────────────────────────────────────

FfmpegVideoOutput::FfmpegVideoOutput(std::shared_ptr<port::IVideoSource> source,
                                     std::string output_path, int width, int height, double fps)
    : pimpl_(
          std::make_unique<Impl>(std::move(source), std::move(output_path), width, height, fps)) {}

FfmpegVideoOutput::~FfmpegVideoOutput() {
    close();
}

// ── onBlock ───────────────────────────────────────────────────────────

void FfmpegVideoOutput::onBlock(const std::optional<VideoSegment>& segment, double duration_sec,
                                double /*block_audio_start_sec*/) {
    if (!pimpl_->open())
        return;

    // Compute how many frames this block should contribute, using an integer
    // accumulator to avoid floating-point drift across thousands of blocks.
    pimpl_->audio_time_acc += duration_sec;
    const auto target_frames = static_cast<int64_t>(pimpl_->audio_time_acc * pimpl_->fps);
    const int64_t needed = target_frames - pimpl_->total_frame_acc;

    if (needed <= 0)
        return;  // Rounding resulted in no new frames this block.

    if (segment.has_value()) {
        const double end_sec = segment->offset_seconds + duration_sec;
        auto frames =
            pimpl_->source->readSegment(segment->source_path, segment->offset_seconds, end_sec);

        if (!frames.empty()) {
            // Emit decoded frames. If we got fewer than needed, repeat the last
            // one to fill. If more, drop the excess.
            for (int64_t i = 0; i < needed; ++i) {
                const std::size_t fi = std::min(static_cast<std::size_t>(i), frames.size() - 1);
                pimpl_->encodeFrame(frames[fi]);
            }
        } else {
            // Decoder produced no frames (pre-roll miss): fill with black.
            for (int64_t i = 0; i < needed; ++i)
                pimpl_->encodeFrame(pimpl_->black_frame);
        }
    } else {
        // Audio-only source block: fill with black frames.
        for (int64_t i = 0; i < needed; ++i)
            pimpl_->encodeFrame(pimpl_->black_frame);
    }

    pimpl_->total_frame_acc = target_frames;
}

// ── close ─────────────────────────────────────────────────────────────

void FfmpegVideoOutput::close() {
    if (!pimpl_->opened || pimpl_->closed)
        return;
    pimpl_->closed = true;

    std::error_code ec;

    // Flush encoder.
    while (true) {
        auto pkt = pimpl_->enc_ctx.encode(ec);
        if (ec || !pkt) {
            ec.clear();
            break;
        }
        {
            const av::Rational etb = pimpl_->enc_ctx.timeBase();
            pkt.raw()->duration = static_cast<int64_t>(std::round(
                static_cast<double>(etb.getDenominator()) / (pimpl_->fps * etb.getNumerator())));
        }
        pkt.setStreamIndex(pimpl_->vid_stream.index());
        pimpl_->fmt_ctx.writePacket(pkt, ec);
    }
    pimpl_->fmt_ctx.writeTrailer(ec);
}

}  // namespace audio::adapter::video
