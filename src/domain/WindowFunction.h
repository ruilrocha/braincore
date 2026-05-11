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
 */
struct WindowFunction {
    /**
     * Multiply @p samples by the chosen window shape in-place.
     * If shape is Rectangle the data is left untouched (identity window).
     */
    static void apply(std::vector<double>& samples, const WindowShape shape) {
        if (shape == WindowShape::Rectangle || samples.empty()) {
            return;
        }

        const auto num_samples = static_cast<double>(samples.size());
        constexpr double pi2 = 2.0 * std::numbers::pi;

        for (auto index = 0; index < samples.size(); ++index) {
            const auto nd = static_cast<double>(index);
            double win_coeff = 1.0;

            switch (shape) {
                case WindowShape::Hamming:
                    win_coeff = 0.53836 - (0.46164 * std::cos(pi2 * nd / (num_samples - 1.0)));
                    break;
                case WindowShape::Hann:
                    win_coeff = 0.5 * (1.0 - std::cos(pi2 * nd / (num_samples - 1.0)));
                    break;
                case WindowShape::Blackman:
                    win_coeff =
                        0.42 - (0.5 * std::cos(pi2 * nd / (num_samples - 1.0))) +
                        (0.08 * std::cos(4.0 * std::numbers::pi * nd / (num_samples - 1.0)));
                    break;
                case WindowShape::Bartlett:
                    win_coeff = 1.0 - ((2.0 * std::fabs(nd - ((num_samples - 1.0) / 2.0))) /
                                       (num_samples - 1.0));
                    break;
                case WindowShape::FlatTop:
                    win_coeff =
                        1.0 - (1.93 * std::cos(pi2 * nd / (num_samples - 1.0))) +
                        (1.29 * std::cos(4.0 * std::numbers::pi * nd / (num_samples - 1.0))) -
                        (0.388 * std::cos(6.0 * std::numbers::pi * nd / (num_samples - 1.0))) +
                        (0.0322 * std::cos(8.0 * std::numbers::pi * nd / (num_samples - 1.0)));
                    break;
                case WindowShape::Gaussian: {
                    constexpr double sigma = 0.5;
                    const double arg =
                        (nd - (num_samples - 1.0) / 2.0) / (sigma * (num_samples - 1.0) / 2.0);
                    win_coeff = std::exp(-0.5 * arg * arg);
                    break;
                }
                case WindowShape::Rectangle:
                    break;  // already handled above
            }
            samples[index] *= win_coeff;
        }
    }

    /**
     * Normalize samples in-place: remove DC offset, then scale peak to ±1.0.
     * Used to produce amplitude-invariant fingerprints for the n_ratio blend.
     */
    static void normalise(std::vector<double>& samples) {
        if (samples.empty()) {
            return;
        }

        // Find min / max.
        double min = samples[0];
        double max = samples[0];
        for (const double sample : samples) {
            min = std::min(sample, min);
            max = std::max(sample, max);
        }

        // Remove DC (centre on zero).
        const double mid = min + ((max - min) / 2.0);
        for (double& sample : samples) {
            sample -= mid;
        }

        // Scale so the largest absolute value is 1.0.
        min -= mid;
        max -= mid;
        double div = std::fabs(min);
        div = std::max(div, max);
        if (div == 0.0) {
            return;  // silent block
        }

        const double inv = 1.0 / div;
        for (double& sample : samples) {
            sample *= inv;
        }
    }
};

}  // namespace audio
