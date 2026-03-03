// miniaudio is header-only; define the implementation in this translation unit.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "MiniaudioOutput.h"

#include <algorithm>
#include <cstring>
#include <thread>

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
                     static_cast<std::size_t>(frame_count));
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

    // Ring buffer: ~0.5 seconds of audio for comfortable latency.
    const auto bs = static_cast<std::size_t>(buffer_size);
    const auto ch = static_cast<std::size_t>(channels);
    const auto sr = static_cast<std::size_t>(sample_rate);
    ring_size_ = std::max(bs * ch * 8, sr * ch);
    ring_.resize(ring_size_, 0.0f);
    ring_read_ = ring_write_ = 0;

    impl_ = new Impl{};

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = static_cast<ma_uint32>(channels);
    config.sampleRate        = static_cast<ma_uint32>(sample_rate);
    config.periodSizeInFrames = static_cast<ma_uint32>(buffer_size);
    config.dataCallback      = dataCallback;
    config.pUserData         = this;

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

    if (impl_) {
        ma_device_uninit(&impl_->device);
        delete impl_;
        impl_ = nullptr;
    }
}

bool MiniaudioOutput::isOpen() const {
    return open_;
}

// ── Write (producer side) ──────────────────────────────────────────────

void MiniaudioOutput::write(const std::vector<double>& samples) {
    // Convert double → float and push into the ring buffer.
    // If the buffer is full, yield briefly (back-pressure).
    for (const double s : samples) {
        const auto fs = static_cast<float>(s);

        for (;;) {
            {
                std::lock_guard lock(ring_mutex_);
                const std::size_t next = (ring_write_ + 1) % ring_size_;
                if (next != ring_read_) {
                    ring_[ring_write_] = fs;
                    ring_write_ = next;
                    break;
                }
            }
            // Buffer full — lock released, yield to let the audio callback drain.
            std::this_thread::yield();
        }
    }
}

// ── Fill buffer (consumer side — audio callback thread) ────────────────

void MiniaudioOutput::fillBuffer(float* output, const std::size_t frame_count) {
    const auto total = frame_count * static_cast<std::size_t>(channels_);

    std::lock_guard lock(ring_mutex_);

    for (std::size_t i = 0; i < total; ++i) {
        if (ring_read_ != ring_write_) {
            output[i] = ring_[ring_read_];
            ring_read_ = (ring_read_ + 1) % ring_size_;
        } else {
            output[i] = 0.0f;  // Underrun — output silence.
        }
    }
}

} // namespace audio::adapter::playback


