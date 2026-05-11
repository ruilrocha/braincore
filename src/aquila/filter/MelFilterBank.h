#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace audio {

/// Sparse entry: a single non-zero filter weight at a given FFT bin.
struct MelWeight {
    std::size_t bin;
    double weight;
};

/**
 * Mel-frequency triangular filter bank stored as a sparse weight matrix.
 *
 * Construction pre-computes all non-zero filter weights. At runtime,
 * `apply()` accepts a pre-computed magnitude spectrum and returns one
 * energy value per filter — no complex arithmetic, no redundant magnitude
 * calculations.
 *
 * C-friendly: all data is in flat vectors, no virtual methods.
 */
class MelFilterBank {
public:
    /**
     * @param sample_rate   Audio sample rate in Hz.
     * @param fft_size      Full FFT size (N, not N/2+1).
     * @param num_filters   Number of Mel filters in the bank.
     * @param mel_filter_width Width of each filter in Mel scale (default 200).
     */
    MelFilterBank(double sample_rate, std::size_t fft_size, std::size_t num_filters = 24,
                  double mel_filter_width = 200.0);

    /**
     * Apply all filters to a pre-computed magnitude spectrum.
     *
     * @param magnitude  Magnitude spectrum of size fft_size/2+1 (half-spectrum).
     * @return Vector of filter energies (size = num_filters).
     */
    [[nodiscard]] std::vector<double> apply(const std::vector<double>& magnitude) const;

    [[nodiscard]] std::size_t size() const { return num_filters_; }

private:
    std::size_t num_filters_;

    // Sparse matrix stored as per-filter lists of (bin, weight) pairs.
    std::vector<std::vector<MelWeight>> weights_;

    static double linearToMel(double f) { return 1127.01048 * std::log(1.0 + (f / 700.0)); }
    static double melToLinear(double m) { return 700.0 * (std::exp(m / 1127.01048) - 1.0); }
};

}  // namespace audio
