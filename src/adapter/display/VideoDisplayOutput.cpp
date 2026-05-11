#include "VideoDisplayOutput.h"

#include <chrono>
#include <thread>

namespace audio::adapter::display {

// ── Constructor ──────────────────────────────────────────────────────────────

VideoDisplayOutput::VideoDisplayOutput(std::shared_ptr<port::IVideoSource> source,
                                       std::shared_ptr<port::IVideoDisplay> display)
    : source_(std::move(source)),
      display_(std::move(display)),
      block_queue_(256)  // 256-slot queue; ample headroom even at small block sizes
{
    decoder_thread_ = std::thread([this] { decoderLoop(); });
}

// ── Destructor ───────────────────────────────────────────────────────────────

VideoDisplayOutput::~VideoDisplayOutput() {
    close();
}

// ── IVideoOutput::onBlock (audio thread) ─────────────────────────────────────

void VideoDisplayOutput::onBlock(const std::optional<VideoSegment>& segment, double duration_sec) {
    if (!running_) {
        return;
    }
    block_queue_.try_enqueue(BlockCmd{.segment = segment, .duration_sec = duration_sec});
}

// ── IVideoOutput::close (any thread) ─────────────────────────────────────────

void VideoDisplayOutput::close() {
    running_ = false;
    if (decoder_thread_.joinable()) {
        decoder_thread_.join();
    }
    // NOTE: display_ lifecycle is owned by the caller (main loop), not here.
}

// ── Decoder loop (Thread B) ──────────────────────────────────────────────────

void VideoDisplayOutput::decoderLoop() {
    while (running_) {
        BlockCmd cmd;
        if (!block_queue_.try_dequeue(cmd)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (cmd.segment && source_) {
            const double start = cmd.segment->offset_seconds;
            const double end = start + cmd.segment->duration_seconds;

            auto frames = source_->readSegment(cmd.segment->source_path, start, end);

            if (!frames.empty()) {
                for (auto& f : frames) {
                    if (!running_) {
                        break;
                    }
                    display_->showFrame(std::move(f));
                }
            } else {
                // No frames decoded — black frame placeholder.
                display_->showFrame(VideoFrame::black(display_->isRunning() ? 1280 : 1, 720));
            }
        } else {
            // Audio-only block — black frame.
            display_->showFrame(VideoFrame::black(1280, 720));
        }
    }
}

}  // namespace audio::adapter::display
