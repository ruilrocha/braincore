#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <vector>

#include "../../domain/port/IAudioOutput.h"

namespace audio::adapter::playback {

/**
 * Real-time audio output using the miniaudio library.
 *
 * Internally runs a lock-free SPSC (single-producer, single-consumer)
 * ring buffer between the caller (which pushes blocks via write()) and
 * the miniaudio audio callback (which drains them).
 *
 * The ring buffer uses power-of-two sizing with atomic indices so the
 * audio callback never blocks.  The producer sleeps on a condition
 * variable when the buffer is full, avoiding CPU-burning busy-waits.
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

    /// Called by the miniaudio callback — do not call directly.
    void fillBuffer(float* output, std::size_t frame_count);

private:
    /// Round up to the next power of two.
    static std::size_t nextPow2(std::size_t v);

    struct Impl;
    Impl* impl_ = nullptr;

    // Lock-free SPSC ring buffer of float samples (interleaved).
    std::vector<float>          ring_;
    std::atomic<std::size_t>    ring_read_{0};
    std::atomic<std::size_t>    ring_write_{0};
    std::size_t                 ring_mask_ = 0;   ///< ring_size - 1 (power-of-two mask).
    std::size_t                 ring_size_ = 0;

    // Condition variable for producer back-pressure (never touched by audio callback lock).
    std::mutex                  wait_mutex_;
    std::condition_variable     ring_not_full_;

    int  channels_ = 0;
    std::atomic<bool> open_{false};
};

} // namespace audio::adapter::playback

