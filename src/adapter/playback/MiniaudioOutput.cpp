// miniaudio is header-only; define the implementation in this translation unit.
#define MINIAUDIO_IMPLEMENTATION
#include "MiniaudioOutput.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <miniaudio.h>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>  // _mm_setcsr / _mm_getcsr for FTZ|DAZ denormal flush
#endif

namespace audio::adapter::playback {

// ── Private implementation (hides miniaudio types from the header) ──────

struct MiniaudioOutput::Impl {
    ma_device device{};  // NOLINT(bugprone-invalid-enum-default-initialization)
};

// ── Miniaudio callback (static trampoline) ─────────────────────────────

static void dataCallback(const ma_device* device, void* output, const void* /*input*/,
                         const ma_uint32 frame_count) {
    auto* self = static_cast<MiniaudioOutput*>(device->pUserData);
    self->fillBuffer(static_cast<float*>(output), frame_count);
}

// ── Construction / destruction ─────────────────────────────────────────

MiniaudioOutput::MiniaudioOutput() = default;

MiniaudioOutput::~MiniaudioOutput() {
    close();
}

// ── Open / close ───────────────────────────────────────────────────────

bool MiniaudioOutput::open(const int sample_rate, const int channels, const int buffer_size) {
    if (open_) {
        close();
    }

    channels_ = channels;
    sample_rate_ = sample_rate;

    // Ring buffer: sized by kRingBufferSeconds so the audio thread has ample
    // headroom against startup CPU transients (video decoder warm-up, cold cache)
    // without the producer blocking.  Adjust kRingBufferSeconds in the header
    // to trade off latency vs. underrun resilience.
    const auto ch = static_cast<std::size_t>(channels);
    const auto sr = static_cast<std::size_t>(sample_rate);
    const auto capacity =
        static_cast<std::size_t>(static_cast<double>(sr * ch) * kRingBufferSeconds);
    ring_ = std::make_unique<moodycamel::ReaderWriterQueue<float>>(capacity);

    impl_ = new Impl{};

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = static_cast<ma_uint32>(channels);
    config.sampleRate = static_cast<ma_uint32>(sample_rate);
    // Let the OS/driver choose its preferred period size for the hardware.
    // Clock granularity is now handled by wall-clock interpolation in
    // getAudioTimeSec() — the hardware period no longer needs to be small.
    config.periodSizeInFrames = 0;
    config.dataCallback = reinterpret_cast<ma_device_data_proc>(dataCallback);
    config.pUserData = this;

    if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    // Seed the wall-clock snapshot so getAudioTimeSec() starts advancing from
    // exactly zero immediately after open(), without waiting for the first
    // hardware callback (which may be up to one period ~85ms away on macOS).
    // Back-date by one hardware period so the initial extrapolation yields 0.
    {
        const ma_uint32 hw_period = impl_->device.playback.internalPeriodSizeInFrames;
        const ma_uint32 hw_sr = impl_->device.playback.internalSampleRate > 0
                                    ? impl_->device.playback.internalSampleRate
                                    : static_cast<ma_uint32>(sample_rate);
        const int64_t period_ns = hw_sr > 0 ? static_cast<int64_t>(hw_period) * 1'000'000'000LL /
                                                  static_cast<int64_t>(hw_sr)
                                            : 0;
        const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count();
        last_callback_samples_ns_.store(0, std::memory_order_relaxed);
        last_callback_wall_ns_.store(now_ns - period_ns, std::memory_order_relaxed);
    }

    open_ = true;
    return true;
}

void MiniaudioOutput::close() {
    if (!open_) {
        return;
    }
    open_ = false;

    // Wake the producer in case it's sleeping on a full buffer.
    ring_not_full_.notify_all();

    if (impl_ != nullptr) {
        ma_device_uninit(&impl_->device);
        delete impl_;
        impl_ = nullptr;
    }
}

bool MiniaudioOutput::isOpen() const {
    return open_;
}

// ── Write (producer side) ──────────────────────────────────────────────
//
// Rule: never drop samples.  Dropping even one float creates an audible
// artifact because the consumer (audio callback) sees a hole in the
// stream.
//
// The old code used wait_for(5 ms) then silently discarded the sample if
// the ring was still full.  With a hardware period of ~85 ms (4096 frames
// @ 48 kHz on macOS) the notify_one() from fillBuffer never arrives within
// those 5 ms, so every sample written while the ring is full gets dropped.
//
// Fix: retry unconditionally.  The wait_for timeout (200 ms) is a
// hard-fail safety-valve only; in practice notify_one() fires within one
// hardware period (≤ 85 ms on macOS / ≤ 23 ms on iOS).

void MiniaudioOutput::write(const std::vector<double>& samples) {
    for (const double samp : samples) {
        if (!open_.load(std::memory_order_relaxed)) {
            return;
        }

        const auto fs = static_cast<float>(samp);

        // Fast path: enqueue without blocking (ring has space).
        if (ring_->try_enqueue(fs)) {
            continue;
        }

        // Slow path: ring is full — wait for the audio callback to drain
        // some samples, then retry.  Never drop.
        while (!ring_->try_enqueue(fs)) {
            if (!open_.load(std::memory_order_relaxed)) {
                return;
            }
            std::unique_lock lock(wait_mutex_);
            // Timeout >> hardware period so we always wake before the next
            // callback fires.  The CV notify_one() in fillBuffer wakes us
            // early in the normal case.
            ring_not_full_.wait_for(lock, std::chrono::milliseconds(200));
        }
    }
}

// ── Fill buffer (consumer side — audio callback thread, lock-free) ─────

void MiniaudioOutput::fillBuffer(float* output, const std::size_t frame_count) {
    // Flush subnormal (denormal) floats to zero once per audio thread.
    // Denormals cause 10–100× slower floating-point on some CPUs and can
    // cause the callback to overrun its time budget.  This is thread-local
    // so it must be set here, not in main().
#ifdef __aarch64__
    {
        static thread_local bool flushed = false;
        if (!flushed) {
            flushed = true;
            uint64_t fpcr = 0ULL;
            __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
            fpcr |= (UINT64_C(1) << 24);  // FZ: flush subnormals to zero
            __asm__ volatile("msr fpcr, %0" ::"r"(fpcr));
        }
    }
#elif defined(__x86_64__) || defined(_M_X64)
    {
        static thread_local bool flushed = false;
        if (!flushed) {
            flushed = true;
            _mm_setcsr(_mm_getcsr() | 0x8040);  // FTZ | DAZ
        }
    }
#endif

    const auto total = frame_count * static_cast<std::size_t>(channels_);

    // Zero the output buffer unconditionally so any underrun gap is clean
    // silence rather than whatever was in the driver's memory.
    std::fill_n(output, total, 0.0F);

    for (std::size_t i = 0; i < total; ++i) {
        float sample = 0.0F;
        if (ring_->try_dequeue(sample)) {
            output[i] = sample;
        }
        // On underrun: 0.0F already written above — no garbage output.
    }

    const auto consumed = samples_consumed_.fetch_add(total, std::memory_order_relaxed) + total;

    // Snapshot wall clock and sample count together so getAudioTimeSec() can
    // interpolate smoothly between hardware callback ticks.
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    last_callback_samples_ns_.store(static_cast<int64_t>(consumed), std::memory_order_relaxed);
    last_callback_wall_ns_.store(now_ns, std::memory_order_relaxed);

    // Wake the producer if it was sleeping on a full buffer.
    ring_not_full_.notify_one();
}

std::size_t MiniaudioOutput::samplesConsumed() const {
    return samples_consumed_.load(std::memory_order_relaxed);
}

double MiniaudioOutput::getAudioTimeSec() const {
    if (!open_ || impl_ == nullptr || channels_ == 0 || sample_rate_ == 0) {
        return 0.0;
    }

    const int64_t cb_samples = last_callback_samples_ns_.load(std::memory_order_relaxed);
    const int64_t cb_wall_ns = last_callback_wall_ns_.load(std::memory_order_relaxed);

    // Before the first callback fires, return 0.
    if (cb_wall_ns == 0) {
        return 0.0;
    }

    // Frames consumed at the last callback.
    const double frames_at_cb = static_cast<double>(cb_samples) / static_cast<double>(channels_);
    const double time_at_cb = frames_at_cb / static_cast<double>(sample_rate_);

    // Elapsed wall-clock time since that callback — extrapolate linearly.
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    const double elapsed_sec = static_cast<double>(now_ns - cb_wall_ns) * 1e-9;

    // Subtract one hardware period: samples in the driver buffer not yet audible.
    const ma_uint32 hw_period = impl_->device.playback.internalPeriodSizeInFrames;
    const ma_uint32 hw_sr = impl_->device.playback.internalSampleRate > 0
                                ? impl_->device.playback.internalSampleRate
                                : static_cast<ma_uint32>(sample_rate_);
    const double hw_latency_sec = static_cast<double>(hw_period) / static_cast<double>(hw_sr);

    return std::max(0.0, time_at_cb + elapsed_sec - hw_latency_sec);
}

}  // namespace audio::adapter::playback