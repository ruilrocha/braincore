#include "VideoDisplayOutput.h"

#include <thread>

namespace audio::adapter::display {

// ── Constructor ──────────────────────────────────────────────────────────────

VideoDisplayOutput::VideoDisplayOutput(std::shared_ptr<port::IVideoSource>  source,
                                       std::shared_ptr<port::IVideoDisplay> display)
    : source_(std::move(source)),
      display_(std::move(display)),
      block_queue_(256)
{
    decoder_thread_ = std::thread([this] { decoderLoop(); });
}

// ── Destructor ───────────────────────────────────────────────────────────────

VideoDisplayOutput::~VideoDisplayOutput() {
    close();
}

// ── IVideoOutput::onBlock (Thread A — audio/processing) ─────────────────────

void VideoDisplayOutput::onBlock(const std::optional<VideoSegment>& segment,
                                 double duration_sec,
                                 double block_audio_start_sec) {
    if (!running_) return;
    block_queue_.try_enqueue(BlockCmd{
        .segment              = segment,
        .duration_sec         = duration_sec,
        .block_audio_start_sec = block_audio_start_sec,
    });
}

// ── IVideoOutput::close (any thread) ─────────────────────────────────────────

void VideoDisplayOutput::close() {
    running_ = false;
    if (decoder_thread_.joinable()) {
        decoder_thread_.join();
    }
}

// ── IVideoOutput::renderFrameForTime (Thread C — main/SDL, ~60 Hz) ──────────

void VideoDisplayOutput::renderFrameForTime(const double audio_time_sec) {
    if (!display_) return;

    std::scoped_lock lock(frames_mutex_);

    // Expire frames whose window has passed.  Keep the last one as a fallback.
    while (!timed_frames_.empty() &&
           timed_frames_.front().audio_end <= audio_time_sec) {
        last_shown_frame_ = std::move(timed_frames_.front().frame);
        timed_frames_.pop_front();
    }

    // Show the frame whose window contains audio_time_sec.
    if (!timed_frames_.empty() &&
        timed_frames_.front().audio_start <= audio_time_sec) {
        last_shown_frame_ = timed_frames_.front().frame;
        display_->showFrame(*last_shown_frame_);
        return;
    }

    // No current frame (gap between blocks or decoder still catching up):
    // repeat the last shown frame to avoid a black screen.
    if (last_shown_frame_) {
        display_->showFrame(*last_shown_frame_);
    }
}

// ── Decoder loop (Thread B) ──────────────────────────────────────────────────

void VideoDisplayOutput::decoderLoop() {
    while (running_) {
        BlockCmd cmd;
        if (!block_queue_.try_dequeue(cmd)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        if (!cmd.segment || !source_) continue;

        const double seg_start = cmd.segment->offset_seconds;
        const double seg_end   = seg_start + cmd.segment->duration_seconds;
        const double seg_dur   = cmd.segment->duration_seconds;

        auto frames = source_->readSegment(cmd.segment->source_path, seg_start, seg_end);
        if (frames.empty()) continue;

        // Map each decoded frame to its audio presentation window.
        //
        // The video segment covers [seg_start, seg_end) in source-file time.
        // The audio block covers [block_audio_start, block_audio_start + duration)
        // in playback time.  We linearly map the frame's source PTS into the
        // block's playback-time window.
        const double block_start = cmd.block_audio_start_sec;
        const double block_dur   = cmd.duration_sec;
        const double first_pts   = frames.front().timestamp_seconds;

        std::vector<TimedFrame> timed;
        timed.reserve(frames.size());

        for (std::size_t i = 0; i < frames.size(); ++i) {
            double frame_offset = 0.0;
            if (seg_dur > 0.0) {
                const double dt = frames[i].timestamp_seconds - first_pts;
                frame_offset = (dt >= 0.0 && dt < seg_dur)
                                   ? (dt / seg_dur * block_dur)
                                   : (static_cast<double>(i) / static_cast<double>(frames.size()) * block_dur);
            } else if (frames.size() > 1) {
                frame_offset = static_cast<double>(i) / static_cast<double>(frames.size()) * block_dur;
            }

            timed.push_back(TimedFrame{
                .audio_start = block_start + frame_offset,
                .audio_end   = 0.0,  // filled below
                .frame       = std::move(frames[i]),
            });
        }

        // Set audio_end for each frame = audio_start of the next; last = block end.
        for (std::size_t i = 0; i + 1 < timed.size(); ++i) {
            timed[i].audio_end = timed[i + 1].audio_start;
        }
        timed.back().audio_end = block_start + block_dur;

        // Commit to shared buffer.
        {
            std::scoped_lock lock(frames_mutex_);
            for (auto& tf : timed) {
                timed_frames_.push_back(std::move(tf));
            }
            // Prevent unbounded growth if the render loop falls behind.
            while (timed_frames_.size() > kMaxBufferedFrames) {
                timed_frames_.pop_front();
            }
        }
    }
}

}  // namespace audio::adapter::display
