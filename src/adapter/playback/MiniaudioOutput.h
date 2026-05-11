#pragma once

#include "../../domain/port/IAudioOutput.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <readerwriterqueue/readerwriterqueue.h>
#include <vector>

namespace audio::adapter::playback {

/**
 * Real-time audio output using the miniaudio library.
 *
 * Uses moodycamel::ReaderWriterQueue (lock-free SPSC) between the
 * caller (which pushes blocks via write()) and the miniaudio audio
 * callback (which drains samples).
 */
class MiniaudioOutput final : public port::IAudioOutput {
public:
    MiniaudioOutput();
    ~MiniaudioOutput() override;

    // Non-copyable, non-movable (owns device handles).
    MiniaudioOutput(const MiniaudioOutput&) = delete;
    MiniaudioOutput& operator=(const MiniaudioOutput&) = delete;

    bool open(int sample_rate, int channels, int buffer_size) override;
    void write(const std::vector<double>& samples) override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] std::size_t samplesConsumed() const override;
    [[nodiscard]] double getAudioTimeSec() const override;

    /// Called by the miniaudio callback — do not call directly.
    void fillBuffer(float* output, std::size_t frame_count);

private:
    struct Impl;
    Impl* impl_ = nullptr;

    std::unique_ptr<moodycamel::ReaderWriterQueue<float>> ring_;
    std::mutex wait_mutex_;
    std::condition_variable ring_not_full_;

    int channels_    = 0;
    int sample_rate_ = 0;
    std::atomic<bool>        open_{false};
    std::atomic<std::size_t> samples_consumed_{0};
};

}  // namespace audio::adapter::playback
