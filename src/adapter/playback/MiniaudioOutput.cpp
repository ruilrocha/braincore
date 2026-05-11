// miniaudio is header-only; define the implementation in this translation unit.
#define MINIAUDIO_IMPLEMENTATION
#include "MiniaudioOutput.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <miniaudio.h>

namespace audio::adapter::playback {

// ── Private implementation (hides miniaudio types from the header) ──────

struct MiniaudioOutput::Impl {
    ma_device device{};
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

    channels_     = channels;
    sample_rate_  = sample_rate;

    // Ring buffer: at most 2 seconds of audio so the audio thread doesn't race
    // more than ~2 seconds ahead of the hardware output.  This keeps the
    // block_audio_start_sec timestamps passed to the video output meaningful.
    const auto ch  = static_cast<std::size_t>(channels);
    const auto sr  = static_cast<std::size_t>(sample_rate);
    const auto bs  = static_cast<std::size_t>(buffer_size);
    const auto capacity = std::min(std::max(bs * ch * 2, sr * ch / 2), sr * ch * 2);
    ring_ = std::make_unique<moodycamel::ReaderWriterQueue<float>>(capacity);

    impl_ = new Impl{};

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = static_cast<ma_uint32>(channels);
    config.sampleRate = static_cast<ma_uint32>(sample_rate);
    config.periodSizeInFrames = static_cast<ma_uint32>(buffer_size);
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

// ── Write (producer side — with CV back-pressure) ──────────────────────

void MiniaudioOutput::write(const std::vector<double>& samples) {
    for (const double s : samples) {
        if (!open_.load(std::memory_order_relaxed)) {
            return;
        }

        const auto fs = static_cast<float>(s);

        // Fast path: try to enqueue without blocking.
        if (ring_->try_enqueue(fs)) {
            continue;
        }

        // Slow path: buffer full — sleep until the consumer drains some.
        {
            std::unique_lock lock(wait_mutex_);
            ring_not_full_.wait_for(lock, std::chrono::milliseconds(5), [&] {
                return ring_->try_enqueue(fs) || !open_.load(std::memory_order_relaxed);
            });
        }

        if (!open_.load(std::memory_order_relaxed)) {
            return;
        }
        // If still full after timeout we silently drop — better than
        // blocking the producer indefinitely.
    }
}

// ── Fill buffer (consumer side — audio callback thread, lock-free) ─────

void MiniaudioOutput::fillBuffer(float* output, const std::size_t frame_count) {
    const auto total = frame_count * static_cast<std::size_t>(channels_);
    float sample = NAN;

    for (std::size_t i = 0; i < total; ++i) {
        if (ring_->try_dequeue(sample)) {
            output[i] = sample;
        } else {
            output[i] = 0.0F;  // Underrun — output silence.
        }
    }

    samples_consumed_.fetch_add(total, std::memory_order_relaxed);

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
    // Frames (non-interleaved) consumed by the hardware driver so far.
    const double frames_consumed =
        static_cast<double>(samples_consumed_.load(std::memory_order_relaxed)) /
        static_cast<double>(channels_);
    const double time_consumed = frames_consumed / static_cast<double>(sample_rate_);

    // Subtract one hardware period: samples currently in the driver output buffer,
    // not yet converted to physical sound.
    const ma_uint32 hw_period = impl_->device.playback.internalPeriodSizeInFrames;
    const ma_uint32 hw_sr     = impl_->device.playback.internalSampleRate > 0
                                    ? impl_->device.playback.internalSampleRate
                                    : static_cast<ma_uint32>(sample_rate_);
    const double hw_latency_sec = static_cast<double>(hw_period) / static_cast<double>(hw_sr);

    return std::max(0.0, time_consumed - hw_latency_sec);
}

}  // namespace audio::adapter::playback