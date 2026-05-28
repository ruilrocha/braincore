#pragma once

#include "BlockConfig.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace audio {

/**
 * Pure-domain utility: apply a window function to audio samples in-place.
 *
 * All math is standard C++ — no external library dependencies.
 *
 * For hot paths (e.g. Brain::addSound), prefer:
 *   1. `makeCoefficients(size, shape)` once per sound to precompute the window.
 *   2. `applyCoefficients(samples, coeffs)` once per block — just a multiply loop.
 */
struct WindowFunction {
    /**
     * Precompute window coefficients for the given shape and block size.
     *
     * Returns an all-ones vector for `Rectangle` (identity window), so
     * `applyCoefficients` can be skipped entirely for that shape.
     */
    [[nodiscard]] static std::vector<double> makeCoefficients(std::size_t size, WindowShape shape) {
        std::vector<double> coeffs(size, 1.0);
        if (shape == WindowShape::Rectangle || size == 0) {
            return coeffs;
        }

        const auto num_samples = static_cast<double>(size);
        constexpr double pi2 = 2.0 * std::numbers::pi;

        for (std::size_t index = 0; index < size; ++index) {
            const auto nd = static_cast<double>(index);
            double w = 1.0;
            switch (shape) {
                case WindowShape::Hamming:
                    w = 0.53836 - (0.46164 * std::cos(pi2 * nd / (num_samples - 1.0)));
                    break;
                case WindowShape::Hann:
                    w = 0.5 * (1.0 - std::cos(pi2 * nd / (num_samples - 1.0)));
                    break;
                case WindowShape::Blackman:
                    w = 0.42 - (0.5 * std::cos(pi2 * nd / (num_samples - 1.0))) +
                        (0.08 * std::cos(4.0 * std::numbers::pi * nd / (num_samples - 1.0)));
                    break;
                case WindowShape::Bartlett:
                    w = 1.0 -
                        ((2.0 * std::fabs(nd - ((num_samples - 1.0) / 2.0))) / (num_samples - 1.0));
                    break;
                case WindowShape::FlatTop:
                    w = 1.0 - (1.93 * std::cos(pi2 * nd / (num_samples - 1.0))) +
                        (1.29 * std::cos(4.0 * std::numbers::pi * nd / (num_samples - 1.0))) -
                        (0.388 * std::cos(6.0 * std::numbers::pi * nd / (num_samples - 1.0))) +
                        (0.0322 * std::cos(8.0 * std::numbers::pi * nd / (num_samples - 1.0)));
                    break;
                case WindowShape::Gaussian: {
                    constexpr double sigma = 0.5;
                    const double arg =
                        (nd - (num_samples - 1.0) / 2.0) / (sigma * (num_samples - 1.0) / 2.0);
                    w = std::exp(-0.5 * arg * arg);
                    break;
                }
                case WindowShape::Rectangle:
                    break;
            }
            coeffs[index] = w;
        }
        return coeffs;
    }

    /**
     * Apply precomputed window coefficients to samples in-place.
     *
     * @p coeffs must be the same size as @p samples. If sizes differ, the
     * shorter length is used (safe but likely a programming error).
     */
    static void applyCoefficients(std::vector<double>& samples, const std::vector<double>& coeffs) {
        const std::size_t n = std::min(samples.size(), coeffs.size());
        for (std::size_t i = 0; i < n; ++i) {
            samples[i] *= coeffs[i];
        }
    }

    /**
     * Convenience overload: compute coefficients on the fly and apply them.
     * Use `makeCoefficients` + `applyCoefficients` in hot paths instead.
     */
    static void apply(std::vector<double>& samples, const WindowShape shape) {
        if (shape == WindowShape::Rectangle || samples.empty()) {
            return;
        }
        const auto coeffs = makeCoefficients(samples.size(), shape);
        applyCoefficients(samples, coeffs);
    }

    /**
     * Normalize samples in-place: remove DC offset, then scale peak to ±1.0.
     * Used to produce amplitude-invariant fingerprints for the n_ratio blend.
     */
    static void normalise(std::vector<double>& samples) {
        if (samples.empty()) {
            return;
        }

        double min = samples[0];
        double max = samples[0];
        for (const double s : samples) {
            if (s < min)
                min = s;
            if (s > max)
                max = s;
        }

        const double mid = min + ((max - min) * 0.5);
        for (double& s : samples) {
            s -= mid;
        }

        min -= mid;
        max -= mid;
        const double peak = std::max(std::fabs(min), std::fabs(max));
        if (peak < 1e-12) {
            return;  // silent block
        }

        const double inv = 1.0 / peak;
        for (double& s : samples) {
            s *= inv;
        }
    }
};

}  // namespace audio
