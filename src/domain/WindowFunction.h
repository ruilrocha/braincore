#pragma once

#include <cmath>
#include <numbers>
#include <vector>

#include "BlockConfig.h"

namespace audio {

/**
 * Pure-domain utility: apply a window function to audio samples in-place.
 *
 * All math is standard C++ — no external library dependencies.
 */
struct WindowFunction {

    /**
     * Multiply @p samples by the chosen window shape in-place.
     * If shape is Rectangle the data is left untouched (identity window).
     */
    static void apply(std::vector<double>& samples, const WindowShape shape) {
        if (shape == WindowShape::Rectangle || samples.empty()) return;

        const auto N = static_cast<double>(samples.size());
        constexpr double pi2 = 2.0 * std::numbers::pi;

        for (std::size_t n = 0; n < samples.size(); ++n) {
            const auto nd = static_cast<double>(n);
            double w = 1.0;

            switch (shape) {
                case WindowShape::Hamming:
                    w = 0.53836 - 0.46164 * std::cos(pi2 * nd / (N - 1.0));
                    break;
                case WindowShape::Hann:
                    w = 0.5 * (1.0 - std::cos(pi2 * nd / (N - 1.0)));
                    break;
                case WindowShape::Blackman:
                    w = 0.42 - 0.5 * std::cos(pi2 * nd / (N - 1.0))
                            + 0.08 * std::cos(4.0 * std::numbers::pi * nd / (N - 1.0));
                    break;
                case WindowShape::Bartlett:
                    w = 1.0 - (2.0 * std::fabs(nd - (N - 1.0) / 2.0)) / (N - 1.0);
                    break;
                case WindowShape::FlatTop:
                    w = 1.0
                        - 1.93  * std::cos(pi2 * nd / (N - 1.0))
                        + 1.29  * std::cos(4.0 * std::numbers::pi * nd / (N - 1.0))
                        - 0.388 * std::cos(6.0 * std::numbers::pi * nd / (N - 1.0))
                        + 0.0322* std::cos(8.0 * std::numbers::pi * nd / (N - 1.0));
                    break;
                case WindowShape::Gaussian: {
                    constexpr double sigma = 0.5;
                    const double arg = (nd - (N - 1.0) / 2.0) / (sigma * (N - 1.0) / 2.0);
                    w = std::exp(-0.5 * arg * arg);
                    break;
                }
                case WindowShape::Rectangle:
                    break; // already handled above
            }
            samples[n] *= w;
        }
    }

    /**
     * Normalise samples in-place: remove DC offset, then scale peak to ±1.0.
     * Used to produce amplitude-invariant fingerprints for the n_ratio blend.
     */
    static void normalise(std::vector<double>& samples) {
        if (samples.empty()) return;

        // Find min / max.
        double mn = samples[0], mx = samples[0];
        for (const double s : samples) {
            if (s < mn) mn = s;
            if (s > mx) mx = s;
        }

        // Remove DC (centre on zero).
        const double mid = mn + (mx - mn) / 2.0;
        for (double& s : samples) s -= mid;

        // Scale so the largest absolute value is 1.0.
        mn -= mid;
        mx -= mid;
        double div = std::fabs(mn);
        if (div < mx) div = mx;
        if (div == 0.0) return; // silent block

        const double inv = 1.0 / div;
        for (double& s : samples) s *= inv;
    }
};

} // namespace audio

