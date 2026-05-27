#include "VideoDisplayOutput.h"

#include <thread>

#ifdef __APPLE__
#include <pthread.h>
#elifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace audio::adapter::display {

// ── Constructor ──────────────────────────────────────────────────────────────

VideoDisplayOutput::VideoDisplayOutput(std::shared_ptr<port::IVideoSource> source,
                                       std::shared_ptr<port::IVideoDisplay> display)
    : source_(std::move(source)), display_(std::move(display)) {
    decoder_thread_ = std::thread([this] {
    // Video decoding is background work — give it lower OS priority so the
    // audio production thread (StreamProcessor) is never preempted by it.
#ifdef __APPLE__
        pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#elifdef __linux__
        struct sched_param sp{};
        sp.sched_priority = 0;
        pthread_setschedparam(pthread_self(), SCHED_IDLE, &sp);
#endif
        decoderLoop();
    });
}

// ── Destructor ───────────────────────────────────────────────────────────────

VideoDisplayOutput::~VideoDisplayOutput() {
    close();
}

// ── IVideoOutput::onBlock (Thread A — audio/processing) ─────────────────────

void VideoDisplayOutput::onBlock(const std::optional<VideoSegment>& segment, double duration_sec,
                                 double block_audio_start_sec) {
    if (!running_) {
        return;
    }
    std::scoped_lock q_lock(block_queue_mutex_);
    block_queue_.push(BlockCmd{
        .segment = segment,
        .duration_sec = duration_sec,
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
    if (!display_) {
        return;
    }

    std::scoped_lock lock(frames_mutex_);

    // Advance past frames only when the *next* frame is also ready to show.
    // This avoids over-eager expiry: if the decoder was late and 20 frames
    // arrive after audio_time has already passed their window, the old logic
    // would pop all 20 instantly.  The new logic keeps the most-current frame
    // visible until a newer one is actually due.
    while (timed_frames_.size() > 1 && timed_frames_[1].audio_start <= audio_time_sec) {
        last_shown_frame_ = std::move(timed_frames_.front().frame);
        timed_frames_.pop_front();
    }

    // Show the front frame if the audio clock has reached it.
    if (!timed_frames_.empty() && timed_frames_.front().audio_start <= audio_time_sec) {
        last_shown_frame_ = timed_frames_.front().frame;
        display_->showFrame(*last_shown_frame_);
        return;
    }

    // Gap or decoder catching up: hold the last shown frame.
    if (last_shown_frame_) {
        display_->showFrame(*last_shown_frame_);
    }
}

// ── Decoder loop (Thread B) ──────────────────────────────────────────────────

void VideoDisplayOutput::decoderLoop() {
    while (running_) {
        BlockCmd cmd;
        bool got_cmd = false;
        {
            std::scoped_lock q_lock(block_queue_mutex_);
            if (!block_queue_.empty()) {
                cmd = std::move(block_queue_.front());
                block_queue_.pop();
                got_cmd = true;
            }
        }
        if (!got_cmd) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        if (!cmd.segment || !source_) {
            continue;
        }

        const double seg_start = cmd.segment->offset_seconds;
        const double seg_end = seg_start + cmd.segment->duration_seconds;

        auto frames = source_->readSegment(cmd.segment->source_path, seg_start, seg_end);
        if (frames.empty()) {
            continue;
        }

        // Distribute frames uniformly across the audio block window.
        //
        // PTS-based mapping is tempting but unreliable: after a seek+flush the
        // PTS spread is often much smaller than the segment duration (e.g. only
        // 0.16 s of PTS range inside a 0.74 s segment), which squeezes all
        // frames into a small fraction of the block and leaves the rest frozen.
        // Index-based uniform spacing guarantees each frame gets an equal share
        // of block_dur regardless of codec-internal timestamp quirks.
        const double block_start = cmd.block_audio_start_sec;
        const double block_dur = cmd.duration_sec;
        const auto frame_count_d = static_cast<double>(frames.size());

        std::vector<TimedFrame> timed;
        timed.reserve(frames.size());

        for (std::size_t i = 0; i < frames.size(); ++i) {
            timed.push_back(TimedFrame{
                .audio_start =
                    (block_start + ((static_cast<double>(i) / frame_count_d) * block_dur)),
                .audio_end =
                    (block_start + ((static_cast<double>(i + 1) / frame_count_d) * block_dur)),
                .frame = std::move(frames[i]),
            });
        }

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
