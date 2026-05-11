#include "FfmpegVideoSource.h"

#include <avcpp/audioresampler.h>
#include <avcpp/av.h>
#include <avcpp/avutils.h>
#include <avcpp/codec.h>
#include <avcpp/codeccontext.h>
#include <avcpp/ffmpeg.h>
#include <avcpp/format.h>
#include <avcpp/formatcontext.h>
#include <avcpp/packet.h>
#include <avcpp/videorescaler.h>
#include <cstring>
#include <list>
#include <mutex>
#include <unordered_map>

// avcodec_flush_buffers is not wrapped by avcpp
extern "C" {
#include <libavcodec/avcodec.h>
}

#include "../../domain/Sound.h"
#include "../../domain/VideoFrame.h"

namespace audio::adapter::video {

// ── Pimpl ─────────────────────────────────────────────────────────────

struct FfmpegVideoSource::Impl {
    // ── Cached open context per video file ────────────────────────────
    struct VideoCtx {
        std::string path;
        av::FormatContext fmt_ctx;
        av::VideoDecoderContext vid_dec;
        ssize_t vid_stream = -1;
        int width = 0;
        int height = 0;
        double fps = 0.0;
        double duration_sec = 0.0;

        // Decoder position tracking — avoids redundant seek+flush for sequential reads.
        double current_pts = -1.0;  ///< PTS of the last decoded frame end. -1 = unknown.

        // When readSegment breaks because pts >= end_seconds, the "break frame" is
        // already decoded but not returned. Buffer it so the next readSegment call
        // can use it without re-seeking.
        std::optional<VideoFrame> buffered_vf;

        VideoCtx() = default;
        VideoCtx(const VideoCtx&) = delete;
        VideoCtx& operator=(const VideoCtx&) = delete;
    };

    using CtxPtr = std::shared_ptr<VideoCtx>;

    std::size_t cache_size;
    int target_sr;
    std::mutex mtx;
    std::list<CtxPtr> lru_list;
    std::unordered_map<std::string, std::list<CtxPtr>::iterator> lru_map;

    explicit Impl(std::size_t cs, int sr) : cache_size(cs), target_sr(sr) {
        av::init();
        av::setFFmpegLoggingLevel(AV_LOG_FATAL);
    }

    // Retrieve (or open & cache) a VideoCtx for path. Returns nullptr on failure.
    CtxPtr acquire(const std::string& path) {
        std::lock_guard lock(mtx);

        auto it = lru_map.find(path);
        if (it != lru_map.end()) {
            lru_list.splice(lru_list.begin(), lru_list, it->second);
            return lru_list.front();
        }

        auto ctx = std::make_shared<VideoCtx>();
        ctx->path = path;

        std::error_code ec;
        ctx->fmt_ctx.openInput(path, ec);
        if (ec)
            return nullptr;
        ctx->fmt_ctx.findStreamInfo(ec);
        if (ec)
            return nullptr;

        for (std::size_t i = 0; i < ctx->fmt_ctx.streamsCount(); ++i) {
            auto st = ctx->fmt_ctx.stream(i);
            if (st.isVideo()) {
                ctx->vid_stream = static_cast<ssize_t>(i);
                ctx->vid_dec = av::VideoDecoderContext(st);
                ctx->vid_dec.open(av::Codec(), ec);
                if (ec) {
                    ctx->vid_stream = -1;
                    break;
                }
                ctx->width = ctx->vid_dec.width();
                ctx->height = ctx->vid_dec.height();
                const auto fr = st.frameRate();
                if (fr.getDenominator() > 0)
                    ctx->fps = fr.getDouble();
                if (const auto dur = ctx->fmt_ctx.duration())
                    ctx->duration_sec = dur.seconds();
                break;
            }
        }

        if (lru_list.size() >= cache_size) {
            lru_map.erase(lru_list.back()->path);
            lru_list.pop_back();
        }
        lru_list.push_front(ctx);
        lru_map[path] = lru_list.begin();
        return ctx;
    }
};

// ── Constructor / Destructor ───────────────────────────────────────────

FfmpegVideoSource::FfmpegVideoSource(std::size_t cache_size, int target_sr)
    : pimpl_(std::make_unique<Impl>(cache_size, target_sr)) {}

FfmpegVideoSource::~FfmpegVideoSource() = default;

// ── loadAudio ─────────────────────────────────────────────────────────

std::unique_ptr<Sound> FfmpegVideoSource::loadAudio(const std::string& path) {
    std::error_code ec;

    av::FormatContext ictx;
    ictx.openInput(path, ec);
    if (ec)
        return nullptr;
    ictx.findStreamInfo(ec);
    if (ec)
        return nullptr;

    // Find first audio stream.
    ssize_t audio_stream = -1;
    for (std::size_t i = 0; i < ictx.streamsCount(); ++i) {
        if (ictx.stream(i).isAudio()) {
            audio_stream = static_cast<ssize_t>(i);
            break;
        }
    }
    if (audio_stream < 0)
        return nullptr;

    av::AudioDecoderContext adec(ictx.stream(static_cast<std::size_t>(audio_stream)));
    adec.open(av::Codec(), ec);
    if (ec)
        return nullptr;

    // Resampler: source layout/rate/format → stereo DBLP @ target_sr_.
    av::AudioResampler resampler(AV_CH_LAYOUT_STEREO, pimpl_->target_sr, av::SampleFormat("dblp"),
                                 adec.channelLayout(), adec.sampleRate(), adec.sampleFormat(), ec);
    if (ec)
        return nullptr;

    constexpr int kOutChannels = 2;
    std::vector<std::vector<double>> channels(kOutChannels);

    auto collect = [&](const av::AudioSamples& out) {
        if (!out)
            return;
        for (int ch = 0; ch < kOutChannels; ++ch) {
            const auto* src =
                reinterpret_cast<const double*>(out.data(static_cast<std::size_t>(ch)));
            channels[static_cast<std::size_t>(ch)].insert(
                channels[static_cast<std::size_t>(ch)].end(), src, src + out.samplesCount());
        }
    };

    auto drainResampler = [&] {
        while (true) {
            auto out = resampler.pop(0, ec);
            if (!out || ec)
                break;
            collect(out);
        }
        ec.clear();
    };

    // Decode loop.
    while (true) {
        auto pkt = ictx.readPacket(ec);
        if (ec || !pkt)
            break;
        if (pkt.streamIndex() != static_cast<size_t>(audio_stream))
            continue;

        auto samples = adec.decode(pkt, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (!samples)
            continue;

        resampler.push(samples, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        drainResampler();
    }
    ec.clear();

    // Flush decoder.
    while (true) {
        av::Packet empty_pkt;
        auto samples = adec.decode(empty_pkt, ec);
        if (ec || !samples) {
            ec.clear();
            break;
        }
        resampler.push(samples, ec);
        if (ec) {
            ec.clear();
            break;
        }
        drainResampler();
    }

    // Drain resampler.
    drainResampler();

    if (channels.empty() || channels[0].empty())
        return nullptr;

    std::vector<audio::Channel> sound_channels(channels.begin(), channels.end());
    return std::make_unique<Sound>(std::move(sound_channels), pimpl_->target_sr);
}

// ── getInfo ───────────────────────────────────────────────────────────

bool FfmpegVideoSource::getInfo(const std::string& path, int& width, int& height, double& fps,
                                double& duration_seconds) {
    auto ctx = pimpl_->acquire(path);
    if (!ctx || ctx->vid_stream < 0)
        return false;
    width = ctx->width;
    height = ctx->height;
    fps = ctx->fps;
    duration_seconds = ctx->duration_sec;
    return true;
}

// ── readFrame ─────────────────────────────────────────────────────────

// ── Shared helpers ────────────────────────────────────────────────────

namespace {

/// Convert an avcpp VideoFrame to a domain VideoFrame (RGB24, scaled to out_w×out_h).
std::optional<VideoFrame> toRgb(const av::VideoFrame& frame, int out_w, int out_h) {
    std::error_code ec;
    av::VideoRescaler rescaler(out_w, out_h, AV_PIX_FMT_RGB24, frame.width(), frame.height(),
                               static_cast<AVPixelFormat>(frame.pixelFormat().get()));
    av::VideoFrame rgb = rescaler.rescale(frame, ec);
    if (ec || !rgb)
        return std::nullopt;

    VideoFrame vf;
    vf.width = out_w;
    vf.height = out_h;
    vf.timestamp_seconds = frame.pts().seconds();
    const std::size_t row_bytes = static_cast<std::size_t>(out_w) * 3;
    vf.pixels.resize(row_bytes * static_cast<std::size_t>(out_h));
    for (int row = 0; row < out_h; ++row) {
        std::memcpy(vf.pixels.data() + static_cast<std::size_t>(row) * row_bytes,
                    rgb.data(0) + row * rgb.raw()->linesize[0], row_bytes);
    }
    return vf;
}

}  // anonymous namespace

// ── readFrame ─────────────────────────────────────────────────────────

std::optional<VideoFrame> FfmpegVideoSource::readFrame(const std::string& path,
                                                       const double time_seconds) {
    auto ctx = pimpl_->acquire(path);
    if (!ctx || ctx->vid_stream < 0)
        return std::nullopt;

    std::error_code ec;
    const av::Timestamp ts{static_cast<int64_t>(time_seconds * av::TimeBase),
                           av::Rational{1, av::TimeBase}};
    ctx->fmt_ctx.seek(ts, ec);
    if (ec)
        return std::nullopt;
    avcodec_flush_buffers(ctx->vid_dec.raw());
    ctx->current_pts = -1.0;

    const double frame_dur = ctx->fps > 0.0 ? 1.0 / ctx->fps : 1.0 / 30.0;

    while (true) {
        auto pkt = ctx->fmt_ctx.readPacket(ec);
        if (ec || !pkt)
            break;
        if (pkt.streamIndex() != static_cast<std::size_t>(ctx->vid_stream))
            continue;

        av::VideoFrame frame = ctx->vid_dec.decode(pkt, ec);
        if (ec || !frame) {
            ec.clear();
            continue;
        }

        if (frame.pts().seconds() + frame_dur <= time_seconds)
            continue;

        ctx->current_pts = frame.pts().seconds() + frame_dur;
        return toRgb(frame, ctx->width, ctx->height);
    }
    return std::nullopt;
}

// ── readSegment ───────────────────────────────────────────────────────

std::vector<VideoFrame> FfmpegVideoSource::readSegment(const std::string& path,
                                                       const double start_seconds,
                                                       const double end_seconds) {
    std::vector<VideoFrame> result;
    if (end_seconds <= start_seconds)
        return result;

    auto ctx = pimpl_->acquire(path);
    if (!ctx || ctx->vid_stream < 0)
        return result;

    std::error_code ec;
    const double frame_dur = ctx->fps > 0.0 ? 1.0 / ctx->fps : 1.0 / 30.0;

    // Only seek when needed: position unknown, backward jump, or too far forward.
    // IMPORTANT: if a buffered frame is available and its end covers start_seconds,
    // never seek — the buffer IS the right position.
    constexpr double kFwdThreshold = 5.0;
    const bool have_buffer = ctx->buffered_vf.has_value();
    const bool buffer_usable = have_buffer &&
                               (ctx->buffered_vf->timestamp_seconds + frame_dur > start_seconds) &&
                               (ctx->buffered_vf->timestamp_seconds < end_seconds);

    const bool need_seek =
        !buffer_usable &&
        ((ctx->current_pts < 0.0) || (start_seconds < ctx->current_pts - frame_dur * 0.5) ||
         (start_seconds > ctx->current_pts + kFwdThreshold));

    if (need_seek) {
        ctx->buffered_vf.reset();
        // Seek 2s before target to give the H264 decoder enough B-frame pre-roll context.
        // Without pre-roll, the first frame output after flush is often well past start_seconds.
        const double seek_time = std::max(0.0, start_seconds - 2.0);
        // Use AVSEEK_FLAG_BACKWARD so we land on the keyframe BEFORE seek_time, not after.
        const auto& st = ctx->fmt_ctx.stream(static_cast<size_t>(ctx->vid_stream));
        const int64_t pts_seek = static_cast<int64_t>(seek_time / av_q2d(st.timeBase().getValue()));
        ctx->fmt_ctx.seek(pts_seek, static_cast<size_t>(ctx->vid_stream), AVSEEK_FLAG_BACKWARD, ec);
        if (ec)
            return result;
        avcodec_flush_buffers(ctx->vid_dec.raw());
        ctx->current_pts = -1.0;
    }

    // If the previous readSegment call buffered the end-of-window frame, use it first.
    // This avoids re-seeking when the next block starts inside the already-decoded frame.
    if (ctx->buffered_vf) {
        VideoFrame& vf = *ctx->buffered_vf;
        if (vf.timestamp_seconds >= end_seconds) {
            // Buffer is still past our window — leave it for the next call.
            return result;
        }
        if (vf.timestamp_seconds + frame_dur > start_seconds) {
            result.push_back(std::move(vf));
        }
        ctx->buffered_vf.reset();
    }

    while (true) {
        auto pkt = ctx->fmt_ctx.readPacket(ec);
        if (ec || !pkt)
            break;
        if (pkt.streamIndex() != static_cast<std::size_t>(ctx->vid_stream))
            continue;

        av::VideoFrame frame = ctx->vid_dec.decode(pkt, ec);
        if (ec || !frame) {
            ec.clear();
            continue;
        }

        const double pts = frame.pts().seconds();

        // Skip frames that end before the window starts (pre-roll drain after seek).
        // Include the frame that CONTAINS start_seconds so video aligns with audio.
        if (pts + frame_dur <= start_seconds) {
            ctx->current_pts = pts + frame_dur;
            continue;
        }

        // Past end of window: buffer for the next call and stop.
        if (pts >= end_seconds) {
            if (auto vf = toRgb(frame, ctx->width, ctx->height)) {
                vf->timestamp_seconds = pts;
                ctx->buffered_vf = std::move(*vf);
            }
            ctx->current_pts = pts;
            break;
        }

        ctx->current_pts = pts + frame_dur;
        if (auto vf = toRgb(frame, ctx->width, ctx->height)) {
            vf->timestamp_seconds = pts;
            result.push_back(std::move(*vf));
        }
    }
    return result;
}

}  // namespace audio::adapter::video
