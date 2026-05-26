// miniaudio is header-only; define the implementation in this translation unit.
#define MINIAUDIO_IMPLEMENTATION
#include "MiniaudioOutput.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <miniaudio.h>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>  // _mm_setcsr / _mm_getcsr for FTZ|DAZ denormal flush
#endif

namespace audio::adapter::playback {

// ── Private implementation (hides miniaudio types from the header) ──────

struct MiniaudioOutput::Impl {
    ma_device device{};    // NOLINT(bugprone-invalid-enum-default-initialization)
    ma_pcm_rb ring_buf{};  // SPSC PCM ring buffer (replaces ReaderWriterQueue)
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

    impl_ = new Impl{};

    // Ring buffer: sized by kRingBufferSeconds so the audio thread has ample
    // headroom against startup CPU transients (video decoder warm-up, cold cache)
    // without the producer blocking.
    const auto cap_frames =
        static_cast<ma_uint32>(static_cast<double>(sample_rate) * kRingBufferSeconds);

    if (ma_pcm_rb_init(ma_format_f32, static_cast<ma_uint32>(channels), cap_frames, nullptr,
                       nullptr, &impl_->ring_buf) != MA_SUCCESS) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }

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
        ma_pcm_rb_uninit(&impl_->ring_buf);
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        ma_pcm_rb_uninit(&impl_->ring_buf);
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
        // Stop the device first — this waits for any in-flight callback to
        // complete, making it safe to uninit the ring buffer immediately after.
        ma_device_uninit(&impl_->device);
        ma_pcm_rb_uninit(&impl_->ring_buf);
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
// artifact because the consumer (audio callback) sees a hole in the stream.
//
// The ring buffer (ma_pcm_rb) is frame-based.  If the ring is full,
// ma_pcm_rb_acquire_write returns 0 frames and we wait on the condition
// variable (notified by fillBuffer) before retrying.  This is the same
// back-pressure model as the previous ReaderWriterQueue implementation.

void MiniaudioOutput::write(const std::vector<double>& samples) {
    std::size_t sample_offset = 0;
    const std::size_t total = samples.size();
    const auto ch = static_cast<std::size_t>(channels_);

    while (sample_offset < total) {
        if (!open_.load(std::memory_order_relaxed)) {
            return;
        }

        const std::size_t samples_left = total - sample_offset;
        const std::size_t frames_left = samples_left / ch;
        if (frames_left == 0) {
            break;  // fewer than one full frame remaining — skip partial frame
        }

        auto frames_to_write = static_cast<ma_uint32>(frames_left);
        void* write_ptr = nullptr;

        if (ma_pcm_rb_acquire_write(&impl_->ring_buf, &frames_to_write, &write_ptr) != MA_SUCCESS ||
            frames_to_write == 0 || write_ptr == nullptr) {
            // Ring is full — wait for the audio callback to drain frames, then retry.
            std::unique_lock lock(wait_mutex_);
            ring_not_full_.wait_for(lock, std::chrono::milliseconds(200));
            continue;
        }

        // Convert double → float and copy into the ring buffer.
        const std::size_t samples_to_copy = frames_to_write * ch;
        auto* dst = static_cast<float*>(write_ptr);
        for (std::size_t i = 0; i < samples_to_copy; ++i) {
            dst[i] = static_cast<float>(samples[sample_offset + i]);
        }

        ma_pcm_rb_commit_write(&impl_->ring_buf, frames_to_write);
        sample_offset += samples_to_copy;
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

    const auto ch = static_cast<std::size_t>(channels_);
    const auto total_samples = frame_count * ch;

    // Zero the output buffer unconditionally so any underrun gap is clean
    // silence rather than whatever was in the driver's memory.
    std::fill_n(output, total_samples, 0.0F);

    // Drain available frames from the ring buffer.  ma_pcm_rb_acquire_read may
    // return fewer than requested if the ring wraps or is partially full —
    // loop to read contiguous chunks until frame_count is satisfied or the
    // ring is empty (underrun).
    std::size_t frame_offset = 0;
    while (frame_offset < frame_count) {
        auto frames_to_read = static_cast<ma_uint32>(frame_count - frame_offset);
        void* read_ptr = nullptr;

        if (ma_pcm_rb_acquire_read(&impl_->ring_buf, &frames_to_read, &read_ptr) != MA_SUCCESS ||
            frames_to_read == 0 || read_ptr == nullptr) {
            break;  // underrun — remaining output stays as silence
        }

        std::memcpy(output + (frame_offset * ch), read_ptr, frames_to_read * ch * sizeof(float));

        ma_pcm_rb_commit_read(&impl_->ring_buf, frames_to_read);
        frame_offset += frames_to_read;
    }

    // Always advance samples_consumed_ by the full hardware period — even on
    // underrun the audio output clock advances in real time (silence is played).
    const auto consumed =
        samples_consumed_.fetch_add(total_samples, std::memory_order_relaxed) + total_samples;

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
