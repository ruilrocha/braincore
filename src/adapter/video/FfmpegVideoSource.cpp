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
#include <libavutil/hwcontext.h>  // av_hwframe_transfer_data
#include <libswscale/swscale.h>   // sws_getContext / sws_scale (HW path)
}

#include "../../domain/Sound.h"
#include "../../domain/VideoFrame.h"

namespace audio::adapter::video {

// ── Hardware-decoder helpers ───────────────────────────────────────────

#ifdef __APPLE__
/// Returns the VideoToolbox decoder name for codec_id, or nullptr if there
/// is no VideoToolbox variant (or the codec is not H264/HEVC).
static const char* hwDecoderName(AVCodecID codec_id) noexcept {
    switch (codec_id) {
        case AV_CODEC_ID_H264:
            return "h264_videotoolbox";
        case AV_CODEC_ID_HEVC:
            return "hevc_videotoolbox";
        default:
            return nullptr;
    }
}
#endif

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
        std::scoped_lock lock(mtx);

        auto it = lru_map.find(path);
        if (it != lru_map.end()) {
            lru_list.splice(lru_list.begin(), lru_list, it->second);
            return lru_list.front();
        }

        auto ctx = std::make_shared<VideoCtx>();
        ctx->path = path;

        std::error_code ec;
        ctx->fmt_ctx.openInput(path, ec);
        if (ec) {
            return nullptr;
        }
        ctx->fmt_ctx.findStreamInfo(ec);
        if (ec) {
            return nullptr;
        }

        for (std::size_t i = 0; i < ctx->fmt_ctx.streamsCount(); ++i) {
            auto st = ctx->fmt_ctx.stream(i);
            if (st.isVideo()) {
                ctx->vid_stream = static_cast<ssize_t>(i);
                ctx->vid_dec = av::VideoDecoderContext(st);

                // ── Hardware decode attempt (macOS: VideoToolbox) ──────────
                // VideoToolbox offloads H264/HEVC to the media engine with
                // near-zero CPU, eliminating audio-thread starvation glitches.
                bool hw_open = false;
#ifdef __APPLE__
                {
                    const auto codec_id = st.raw()->codecpar->codec_id;
                    if (const char* hw_name = hwDecoderName(codec_id)) {
                        if (const AVCodec* hw_codec = avcodec_find_decoder_by_name(hw_name)) {
                            std::error_code hw_ec;
                            ctx->vid_dec.open(av::Codec(hw_codec), hw_ec);
                            hw_open = !hw_ec;
                        }
                    }
                }
#endif
                // ── Software fallback ──────────────────────────────────────
                if (!hw_open) {
                    // Re-create the context: a failed open() may leave it dirty.
                    ctx->vid_dec = av::VideoDecoderContext(st);
                    // Limit threads so video decode doesn't starve the audio
                    // production thread.  1 thread is sufficient for 480–720p.
                    ctx->vid_dec.raw()->thread_count = 1;
                    ctx->vid_dec.open(av::Codec(), ec);
                    if (ec) {
                        ctx->vid_stream = -1;
                        break;
                    }
                }
                ctx->width = ctx->vid_dec.width();
                ctx->height = ctx->vid_dec.height();
                const auto fr = st.frameRate();
                if (fr.getDenominator() > 0) {
                    ctx->fps = fr.getDouble();
                }
                if (const auto dur = ctx->fmt_ctx.duration()) {
                    ctx->duration_sec = dur.seconds();
                }
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
    if (ec) {
        return nullptr;
    }
    ictx.findStreamInfo(ec);
    if (ec) {
        return nullptr;
    }

    // Find first audio stream.
    ssize_t audio_stream = -1;
    for (std::size_t i = 0; i < ictx.streamsCount(); ++i) {
        if (ictx.stream(i).isAudio()) {
            audio_stream = static_cast<ssize_t>(i);
            break;
        }
    }
    if (audio_stream < 0) {
        return nullptr;
    }

    av::AudioDecoderContext adec(ictx.stream(static_cast<std::size_t>(audio_stream)));
    adec.open(av::Codec(), ec);
    if (ec) {
        return nullptr;
    }

    // Resampler: source layout/rate/format → stereo DBLP @ target_sr_.
    av::AudioResampler resampler(AV_CH_LAYOUT_STEREO, pimpl_->target_sr, av::SampleFormat("dblp"),
                                 adec.channelLayout(), adec.sampleRate(), adec.sampleFormat(), ec);
    if (ec) {
        return nullptr;
    }

    constexpr int kOutChannels = 2;
    std::vector<std::vector<double>> channels(kOutChannels);

    auto collect = [&](const av::AudioSamples& out) {
        if (!out) {
            return;
        }
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
            if (!out || ec) {
                break;
            }
            collect(out);
        }
        ec.clear();
    };

    // Decode loop.
    while (true) {
        auto pkt = ictx.readPacket(ec);
        if (ec || !pkt) {
            break;
        }
        if (pkt.streamIndex() != static_cast<std::size_t>(audio_stream)) {
            continue;
        }

        auto samples = adec.decode(pkt, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (!samples) {
            continue;
        }

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

    if (channels.empty() || channels[0].empty()) {
        return nullptr;
    }

    std::vector<audio::Channel> sound_channels(channels.begin(), channels.end());
    return std::make_unique<Sound>(std::move(sound_channels), pimpl_->target_sr);
}

// ── getInfo ───────────────────────────────────────────────────────────

bool FfmpegVideoSource::getInfo(const std::string& path, int& width, int& height, double& fps,
                                double& duration_seconds) {
    auto ctx = pimpl_->acquire(path);
    if (!ctx || ctx->vid_stream < 0) {
        return false;
    }
    width = ctx->width;
    height = ctx->height;
    fps = ctx->fps;
    duration_seconds = ctx->duration_sec;
    return true;
}

// ── Shared helpers ────────────────────────────────────────────────────

namespace {

/// Returns true for any AVPixelFormat whose plane layout is identical to
/// AV_PIX_FMT_YUV420P (3 separate planes, luma full-res, chroma half-res).
/// YUVJ420P is the "full colour range" variant but has exactly the same memory
/// layout; SDL and the GPU handle the colour-range difference.
inline bool isYuv420pCompat(AVPixelFormat fmt) noexcept {
    return fmt == AV_PIX_FMT_YUV420P || fmt == AV_PIX_FMT_YUVJ420P;
}

/// Copy a single plane from a possibly-padded AVFrame into a tightly-packed
/// destination buffer.  When src_stride == width the entire plane is a single
/// contiguous block and a bulk memcpy is used instead of per-row calls.
inline void copyPlane(uint8_t* dst, const uint8_t* src, int src_stride, int width,
                      int height) noexcept {
    if (src_stride == width) {
        std::memcpy(dst, src, static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    } else {
        for (int row = 0; row < height; ++row) {
            std::memcpy(
                dst + (static_cast<std::size_t>(row) * static_cast<std::size_t>(width)),
                src + (static_cast<std::size_t>(row) * static_cast<std::size_t>(src_stride)),
                static_cast<std::size_t>(width));
        }
    }
}

/// Convert a raw AVFrame (YUV420P-family, correct dimensions) into a domain
/// VideoFrame without rescaling.  Returns nullopt for other formats —
/// caller must rescale first.
std::optional<VideoFrame> avframe_to_videoframe(const AVFrame* raw, int out_w, int out_h,
                                                double pts_sec) {
    const auto fmt = static_cast<AVPixelFormat>(raw->format);
    if (!isYuv420pCompat(fmt)) {
        return std::nullopt;
    }

    const int uv_w = out_w / 2;
    const int uv_h = out_h / 2;

    VideoFrame vf;
    vf.width = out_w;
    vf.height = out_h;
    vf.timestamp_seconds = pts_sec;

    auto yuv = Yuv420pData::make(out_w, out_h);
    copyPlane(yuv.y.data.data(), raw->data[0], raw->linesize[0], out_w, out_h);
    copyPlane(yuv.u.data.data(), raw->data[1], raw->linesize[1], uv_w, uv_h);
    copyPlane(yuv.v.data.data(), raw->data[2], raw->linesize[2], uv_w, uv_h);
    vf.pixels = std::move(yuv);
    return vf;
}

/// Copy a single NV12 frame from an AVFrame into a domain Nv12Data.
/// NV12 has two planes: Y (full res) and UV (interleaved, half height).
std::optional<VideoFrame> avframe_to_nv12frame(const AVFrame* raw, int out_w, int out_h,
                                               double pts_sec) {
    VideoFrame vf;
    vf.width = out_w;
    vf.height = out_h;
    vf.timestamp_seconds = pts_sec;

    auto nv12 = Nv12Data::make(out_w, out_h);
    copyPlane(nv12.y.data.data(), raw->data[0], raw->linesize[0], out_w, out_h);
    copyPlane(nv12.uv.data.data(), raw->data[1], raw->linesize[1], out_w, out_h / 2);
    vf.pixels = std::move(nv12);
    return vf;
}

/// Convert an avcpp VideoFrame into a domain VideoFrame.
///
/// Hardware path (VideoToolbox → CPU transfer):
///   `av_hwframe_transfer_data` gives NV12 in system memory on macOS.
///   We copy the two planes directly into an Nv12Data — **no sws_scale**.
///   SDL3 renders NV12 natively via SDL_PIXELFORMAT_NV12 + SDL_UpdateNVTexture,
///   so the GPU handles the YUV→RGB conversion in the fragment shader.
///   This eliminates the only remaining heavy CPU operation in the video path.
///
///   Fallback for non-NV12 HW output or unexpected format: sws_scale to YUV420P.
///
/// Software path:
///   Fast: YUV420P / YUVJ420P at correct dimensions → bulk memcpy.
///   Slow: VideoRescaler (sws_scale) → YUV420P.
std::optional<VideoFrame> toVideoFrame(const av::VideoFrame& frame, int out_w, int out_h) {
    const auto fmt = static_cast<AVPixelFormat>(frame.pixelFormat().get());

    // ── Hardware frames (e.g. VideoToolbox) ───────────────────────────
    // sws_scale cannot operate on GPU-backed pixel formats.  Transfer to
    // system memory first, then handle the CPU-side format.
    if (fmt == AV_PIX_FMT_VIDEOTOOLBOX) {
        struct AvFrameGuard {
            AVFrame* f;
            ~AvFrameGuard() noexcept {
                if (f != nullptr) {
                    av_frame_free(&f);
                }
            }
        };

        AvFrameGuard cpu{av_frame_alloc()};
        if (cpu.f == nullptr || av_hwframe_transfer_data(cpu.f, frame.raw(), 0) != 0) {
            return std::nullopt;
        }
        cpu.f->pts = frame.raw()->pts;
        cpu.f->best_effort_timestamp = frame.raw()->best_effort_timestamp;

        const auto cpu_fmt = static_cast<AVPixelFormat>(cpu.f->format);
        const int src_w = cpu.f->width;
        const int src_h = cpu.f->height;

        // Primary fast path: VideoToolbox → NV12 (typical on macOS).
        // Copy planes directly — no sws_scale, no format conversion on CPU.
        if (cpu_fmt == AV_PIX_FMT_NV12 && src_w == out_w && src_h == out_h) {
            return avframe_to_nv12frame(cpu.f, out_w, out_h, frame.pts().seconds());
        }

        // Planar YUV420P (rare but possible).
        if (isYuv420pCompat(cpu_fmt) && src_w == out_w && src_h == out_h) {
            return avframe_to_videoframe(cpu.f, out_w, out_h, frame.pts().seconds());
        }

        // Fallback: unexpected HW output format → sws_scale to YUV420P.
        AvFrameGuard yuv{av_frame_alloc()};
        if (yuv.f == nullptr) {
            return std::nullopt;
        }
        yuv.f->format = AV_PIX_FMT_YUV420P;
        yuv.f->width = out_w;
        yuv.f->height = out_h;
        if (av_frame_get_buffer(yuv.f, 0) < 0) {
            return std::nullopt;
        }

        SwsContext* sws = sws_getContext(src_w, src_h, cpu_fmt, out_w, out_h, AV_PIX_FMT_YUV420P,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (sws == nullptr) {
            return std::nullopt;
        }
        sws_scale(sws, static_cast<const uint8_t* const*>(cpu.f->data), cpu.f->linesize, 0, src_h,
                  yuv.f->data, yuv.f->linesize);
        sws_freeContext(sws);
        return avframe_to_videoframe(yuv.f, out_w, out_h, frame.pts().seconds());
    }

    // ── Software frames ────────────────────────────────────────────────
    // Fast path: no conversion needed.
    if (isYuv420pCompat(fmt) && frame.width() == out_w && frame.height() == out_h) {
        return avframe_to_videoframe(frame.raw(), out_w, out_h, frame.pts().seconds());
    }

    // Fallback: sws_scale to YUV420P.
    std::error_code ec;
    av::VideoRescaler rescaler(out_w, out_h, AV_PIX_FMT_YUV420P, frame.width(), frame.height(),
                               fmt);
    av::VideoFrame yuv = rescaler.rescale(frame, ec);
    if (ec || yuv.raw() == nullptr) {
        return std::nullopt;
    }
    return avframe_to_videoframe(yuv.raw(), out_w, out_h, frame.pts().seconds());
}

}  // anonymous namespace

// ── readSegment ───────────────────────────────────────────────────────

std::vector<VideoFrame> FfmpegVideoSource::readSegment(const std::string& path,
                                                       const double start_seconds,
                                                       const double end_seconds) {
    std::vector<VideoFrame> result;
    if (end_seconds <= start_seconds) {
        return result;
    }

    auto ctx = pimpl_->acquire(path);
    if (!ctx || ctx->vid_stream < 0) {
        return result;
    }

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
        ((ctx->current_pts < 0.0) || (start_seconds < ctx->current_pts - (frame_dur * 0.5)) ||
         (start_seconds > ctx->current_pts + kFwdThreshold));

    if (need_seek) {
        ctx->buffered_vf.reset();
        // Seek 2s before target to give the H264 decoder enough B-frame pre-roll context.
        // Without pre-roll, the first frame output after flush is often well past start_seconds.
        const double seek_time = std::max(0.0, start_seconds - 2.0);
        // Use AVSEEK_FLAG_BACKWARD so we land on the keyframe BEFORE seek_time, not after.
        const auto& st = ctx->fmt_ctx.stream(static_cast<std::size_t>(ctx->vid_stream));
        const int64_t pts_seek = static_cast<int64_t>(seek_time / av_q2d(st.timeBase().getValue()));
        ctx->fmt_ctx.seek(pts_seek, static_cast<int>(ctx->vid_stream), AVSEEK_FLAG_BACKWARD, ec);
        if (ec) {
            return result;
        }
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
        if (ec || !pkt) {
            break;
        }
        if (pkt.streamIndex() != static_cast<std::size_t>(ctx->vid_stream)) {
            continue;
        }

        av::VideoFrame frame = ctx->vid_dec.decode(pkt, ec);
        if (ec || !frame) {
            ec.clear();
            continue;
        }

        const double pts = frame.pts().seconds();

        // Skip frames that end before the window starts (pre-roll drain after seek).
        if (pts + frame_dur <= start_seconds) {
            ctx->current_pts = pts + frame_dur;
            continue;
        }

        // Past end of window: buffer for the next call and stop.
        if (pts >= end_seconds) {
            if (auto vf = toVideoFrame(frame, ctx->width, ctx->height)) {
                vf->timestamp_seconds = pts;
                ctx->buffered_vf = std::move(*vf);
            }
            ctx->current_pts = pts;
            break;
        }

        ctx->current_pts = pts + frame_dur;
        if (auto vf = toVideoFrame(frame, ctx->width, ctx->height)) {
            vf->timestamp_seconds = pts;
            result.push_back(std::move(*vf));
        }
    }
    return result;
}

}  // namespace audio::adapter::video
