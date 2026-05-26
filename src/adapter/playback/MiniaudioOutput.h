#pragma once

#include "../../domain/port/IAudioOutput.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <vector>

namespace audio::adapter::playback {

/**
 * Real-time audio output using the miniaudio library.
 *
 * Uses miniaudio's built-in `ma_pcm_rb` (lock-free SPSC PCM ring buffer)
 * between the caller (which pushes blocks via write()) and the miniaudio
 * audio callback (which drains frames).  The ring buffer is owned by the
 * private Impl struct so that miniaudio types do not leak into this header.
 */
class MiniaudioOutput final : public port::IAudioOutput {
public:
    // ── Tuning constants ─────────────────────────────────────────────────
    //
    // Ring buffer duration (seconds).  Larger values give more tolerance for
    // CPU spikes (video decode, MFCC search) at startup; smaller values lower
    // end-to-end latency.  Values below 1.0 s risk underruns on slow machines.
    // Typical useful range: 0.5 – 8.0 s.
    static constexpr double kRingBufferSeconds = 1.0;
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

    std::mutex wait_mutex_;
    std::condition_variable ring_not_full_;

    int channels_ = 0;
    int sample_rate_ = 0;
    std::atomic<bool> open_{false};
    std::atomic<std::size_t> samples_consumed_{0};

    // Wall-clock interpolation for getAudioTimeSec().
    // The hardware callback fires infrequently (one large period) but we need
    // the audio clock to advance smoothly at render rate (60 Hz).
    // We record the samples_consumed and wall-clock time at each callback, then
    // extrapolate linearly using std::chrono between callbacks.
    std::atomic<int64_t> last_callback_samples_ns_{0};  // samples at last callback
    std::atomic<int64_t> last_callback_wall_ns_{0};     // steady_clock ns at last callback
};

}  // namespace audio::adapter::playback
