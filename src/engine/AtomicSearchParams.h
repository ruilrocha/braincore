#pragma once

#include "../domain/SearchParams.h"

#include <atomic>

namespace audio {

/**
 * Thread-safe snapshot of all live search parameters.
 *
 * Fields use `std::memory_order_relaxed` atomics so a UI thread can write
 * continuously while the audio thread reads without a mutex.  A stale-by-one-
 * block read is acceptable for all parameters.
 *
 * Usage:
 *   // UI / control thread:
 *   params.mel_weight.store(0.5, std::memory_order_relaxed);
 *
 *   // Audio thread (once per block):
 *   SearchParams sp = params.snapshot();
 */
struct AtomicSearchParams {
    std::atomic<double> stickyness{0.0};
    std::atomic<double> usage_weight{0.0};
    std::atomic<double> usage_falloff{1.0};
    std::atomic<double> mfcc_weight{0.0};
    std::atomic<double> mel_weight{1.0};
    std::atomic<double> spectral_weight{0.0};
    std::atomic<double> n_ratio{0.0};

    /// Copy all fields into a plain SearchParams value object.
    [[nodiscard]] SearchParams snapshot() const noexcept {
        SearchParams sp;
        sp.stickyness = stickyness.load(std::memory_order_relaxed);
        sp.usage_weight = usage_weight.load(std::memory_order_relaxed);
        sp.usage_falloff = usage_falloff.load(std::memory_order_relaxed);
        sp.mfcc_weight = mfcc_weight.load(std::memory_order_relaxed);
        sp.mel_weight = mel_weight.load(std::memory_order_relaxed);
        sp.spectral_weight = spectral_weight.load(std::memory_order_relaxed);
        sp.n_ratio = n_ratio.load(std::memory_order_relaxed);
        return sp;
    }
};

}  // namespace audio
