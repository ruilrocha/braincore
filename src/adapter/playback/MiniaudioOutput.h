#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <vector>

#include "../../domain/port/IAudioOutput.h"

namespace audio::adapter::playback {

/**
 * Real-time audio output using the miniaudio library.
 *
 * Internally runs a ring buffer between the caller (which pushes blocks
 * via write()) and the miniaudio audio callback (which drains them).
 * The ring buffer is protected by a lightweight spinlock to keep the
 * audio thread wait-free in the common case.
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

    // Called by the miniaudio callback — do not call directly.
    void fillBuffer(float* output, std::size_t frame_count);

private:
    struct Impl;
    Impl* impl_ = nullptr;

    // Ring buffer of float samples (interleaved).
    std::vector<float> ring_;
    std::size_t        ring_read_  = 0;
    std::size_t        ring_write_ = 0;
    std::size_t        ring_size_  = 0;
    std::mutex         ring_mutex_;

    int  channels_    = 0;
    std::atomic<bool> open_{false};
};

} // namespace audio::adapter::playback

