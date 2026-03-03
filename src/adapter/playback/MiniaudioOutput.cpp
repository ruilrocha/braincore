// miniaudio is header-only; define the implementation in this translation unit.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "MiniaudioOutput.h"

#include <algorithm>
#include <chrono>

namespace audio::adapter::playback {

// ── Private implementation (hides miniaudio types from the header) ──────

struct MiniaudioOutput::Impl {
    ma_device device{};
};

// ── Miniaudio callback (static trampoline) ─────────────────────────────

static void dataCallback(ma_device* device, void* output,
                          const void* /*input*/, ma_uint32 frame_count) {
    auto* self = static_cast<MiniaudioOutput*>(device->pUserData);
    self->fillBuffer(static_cast<float*>(output),
                     frame_count);
}

// ── Helpers ────────────────────────────────────────────────────────────

std::size_t MiniaudioOutput::nextPow2(std::size_t v) {
    if (v == 0) return 1;
    --v;
    v |= v >> 1;  v |= v >> 2;  v |= v >> 4;
    v |= v >> 8;  v |= v >> 16; v |= v >> 32;
    return v + 1;
}

// ── Construction / destruction ─────────────────────────────────────────

MiniaudioOutput::MiniaudioOutput() = default;

MiniaudioOutput::~MiniaudioOutput() {
    close();
}

// ── Open / close ───────────────────────────────────────────────────────

bool MiniaudioOutput::open(const int sample_rate, const int channels,
                            const int buffer_size) {
    if (open_) close();

    channels_ = channels;

    // Ring buffer: ~1 second of audio, rounded up to power-of-two for
    // efficient modular indexing via bitmask.
    const auto bs = static_cast<std::size_t>(buffer_size);
    const auto ch = static_cast<std::size_t>(channels);
    const auto sr = static_cast<std::size_t>(sample_rate);
    ring_size_ = nextPow2(std::max(bs * ch * 8, sr * ch));
    ring_mask_ = ring_size_ - 1;
    ring_.assign(ring_size_, 0.0f);
    ring_read_.store(0, std::memory_order_relaxed);
    ring_write_.store(0, std::memory_order_relaxed);

    impl_ = new Impl{};

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format    = ma_format_f32;
    config.playback.channels  = static_cast<ma_uint32>(channels);
    config.sampleRate         = static_cast<ma_uint32>(sample_rate);
    config.periodSizeInFrames = static_cast<ma_uint32>(buffer_size);
    config.dataCallback       = dataCallback;
    config.pUserData          = this;

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

    open_ = true;
    return true;
}

void MiniaudioOutput::close() {
    if (!open_) return;
    open_ = false;

    // Wake the producer in case it's sleeping on a full buffer.
    ring_not_full_.notify_all();

    if (impl_) {
        ma_device_uninit(&impl_->device);
        delete impl_;
        impl_ = nullptr;
    }
}

bool MiniaudioOutput::isOpen() const {
    return open_;
}

// ── Write (producer side — lock-free with CV back-pressure) ────────────

void MiniaudioOutput::write(const std::vector<double>& samples) {
    for (const double s : samples) {
        if (!open_.load(std::memory_order_relaxed)) return;

        const auto fs = static_cast<float>(s);
        const std::size_t w = ring_write_.load(std::memory_order_relaxed);
        std::size_t next = (w + 1) & ring_mask_;

        // Fast path: space available — no lock needed.
        if (next != ring_read_.load(std::memory_order_acquire)) {
            ring_[w] = fs;
            ring_write_.store(next, std::memory_order_release);
            continue;
        }

        // Slow path: buffer full — sleep until the consumer drains some.
        {
            std::unique_lock lock(wait_mutex_);
            ring_not_full_.wait_for(lock, std::chrono::milliseconds(5), [&] {
                return (next != ring_read_.load(std::memory_order_acquire))
                       || !open_.load(std::memory_order_relaxed);
            });
        }

        if (!open_.load(std::memory_order_relaxed)) return;

        // Re-check after waking — might still be full if we timed out.
        next = (ring_write_.load(std::memory_order_relaxed) + 1) & ring_mask_;
        if (next != ring_read_.load(std::memory_order_acquire)) {
            const std::size_t w2 = ring_write_.load(std::memory_order_relaxed);
            ring_[w2] = fs;
            ring_write_.store((w2 + 1) & ring_mask_, std::memory_order_release);
        }
        // If still full after timeout we silently drop the sample — better
        // than blocking the producer indefinitely.
    }
}

// ── Fill buffer (consumer side — audio callback thread, lock-free) ─────

void MiniaudioOutput::fillBuffer(float* output, const std::size_t frame_count) {
    const auto total = frame_count * static_cast<std::size_t>(channels_);
    const std::size_t w = ring_write_.load(std::memory_order_acquire);
    std::size_t r = ring_read_.load(std::memory_order_relaxed);

    for (std::size_t i = 0; i < total; ++i) {
        if (r != w) {
            output[i] = ring_[r];
            r = (r + 1) & ring_mask_;
        } else {
            output[i] = 0.0f;  // Underrun — output silence.
        }
    }

    ring_read_.store(r, std::memory_order_release);

    // Wake the producer if it was sleeping on a full buffer.
    ring_not_full_.notify_one();
}

} // namespace audio::adapter::playback


