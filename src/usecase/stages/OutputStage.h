#pragma once

#include "../../domain/port/IAudioOutput.h"
#include "../../domain/port/IRecorder.h"
#include "../../domain/port/IVideoOutput.h"
#include "../IBlockStage.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace audio::usecase::stages {

/**
 * Pipeline stage 4: Output.
 *
 * Interleaves ctx.channel_outputs, writes to IAudioOutput, optionally tees
 * to an IRecorder, and notifies the IVideoOutput with the matched segment.
 *
 * Pre-allocates the interleaved sample buffer at construction (resizes if
 * block size ever changes) — no per-block heap allocation on the hot path.
 *
 * Thread-safety:
 *   - process() is called from the audio processing thread.
 *   - setRecorder() may be called from any thread (protected by recorder_mutex_).
 */
class OutputStage final : public IBlockStage {
public:
    /**
     * @param output      Audio output device (required).
     * @param recorder    Optional initial recorder (can be swapped via setRecorder).
     * @param video_out   Optional video output consumer.
     * @param sample_rate Playback sample rate (Hz) — used for video timing.
     */
    OutputStage(std::shared_ptr<port::IAudioOutput> output,
                std::shared_ptr<port::IRecorder> recorder = nullptr,
                std::shared_ptr<port::IVideoOutput> video_out = nullptr, int sample_rate = 48000);

    void process(BlockContext& ctx) override;

    /// Swap the recorder at runtime (thread-safe).
    void setRecorder(std::shared_ptr<port::IRecorder> recorder);

    /// Total interleaved samples written since construction (for A/V sync).
    [[nodiscard]] std::size_t totalSamplesWritten() const {
        return total_samples_written_.load(std::memory_order_relaxed);
    }

private:
    std::shared_ptr<port::IAudioOutput> output_;
    std::shared_ptr<port::IRecorder> recorder_;
    std::shared_ptr<port::IVideoOutput> video_out_;
    int sample_rate_;

    mutable std::mutex recorder_mutex_;

    // Pre-allocated interleaved buffer (avoids per-block heap allocation).
    std::vector<double> interleaved_;

    std::atomic<std::size_t> total_samples_written_{0};
};

}  // namespace audio::usecase::stages
