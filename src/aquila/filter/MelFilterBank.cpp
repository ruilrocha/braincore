#include "MelFilterBank.h"

#include <algorithm>

namespace audio {

MelFilterBank::MelFilterBank(const double sample_rate, const std::size_t fft_size,
                             const std::size_t num_filters, const double mel_filter_width)
    : num_filters_(num_filters), weights_(num_filters) {
    const auto half_n = (fft_size / 2) + 1;

    for (std::size_t f = 0; f < num_filters; ++f) {
        // Mel-scale triangle boundaries.
        const double mel_min = static_cast<double>(f) * mel_filter_width / 2.0;
        const double mel_center = mel_min + (mel_filter_width / 2.0);
        const double mel_max = mel_min + mel_filter_width;

        const double freq_min = melToLinear(mel_min);
        const double freq_center = melToLinear(mel_center);
        const double freq_max = melToLinear(mel_max);

        // Bin range for this triangle.
        const auto bin_min =
            static_cast<std::size_t>(freq_min * static_cast<double>(fft_size) / sample_rate);
        auto bin_max =
            static_cast<std::size_t>(freq_max * static_cast<double>(fft_size) / sample_rate);
        bin_max = std::min(bin_max, half_n - 1);

        if (bin_max <= bin_min) {
            continue;
        }

        // Only store non-zero weights (sparse).
        for (std::size_t k = bin_min; k <= bin_max; ++k) {
            const double freq_k =
                static_cast<double>(k) * sample_rate / static_cast<double>(fft_size);

            double w = 0.0;
            if (freq_k >= freq_min && freq_k < freq_center) {
                w = (freq_k - freq_min) / (freq_center - freq_min);
            } else if (freq_k >= freq_center && freq_k < freq_max) {
                w = (freq_max - freq_k) / (freq_max - freq_center);
            }

            if (w > 0.0) {
                weights_[f].push_back({.bin = k, .weight = w});
            }
        }
    }
}

std::vector<double> MelFilterBank::apply(const std::vector<double>& magnitude) const {
    std::vector energies(num_filters_, 0.0);

    for (std::size_t f = 0; f < num_filters_; ++f) {
        double sum = 0.0;
        for (const auto& [bin, weight] : weights_[f]) {
            if (bin < magnitude.size()) {
                sum += magnitude[bin] * weight;
            }
        }
        energies[f] = sum;
    }

    return energies;
}

}  // namespace audio
