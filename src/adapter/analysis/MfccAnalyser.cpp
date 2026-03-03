#include "MfccAnalyser.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

#include <fftw3.h>
#include "../../aquila/filter/MelFilterBank.h"
#include "../../aquila/transform/Dct.h"

namespace audio::adapter::analysis {

MfccAnalyser::MfccAnalyser(const int num_mfcc, const int num_fft_bins)
    : num_mfcc_(num_mfcc), num_fft_bins_(num_fft_bins) {}

// ── Internal FFT helper ────────────────────────────────────────────────

namespace {

/**
 * Run FFTW real-to-complex and return the complex spectrum plus the
 * magnitude-per-bin array.
 */
struct FftResult {
    Aquila::SpectrumType spectrum;
    std::vector<double>  magnitude_bins;
    std::size_t          half_n;
};

FftResult runFft(const std::vector<double>& block) {
    const auto N = static_cast<int>(block.size());
    const auto half = block.size() / 2 + 1;

    std::vector<double> buf(block.begin(), block.end());

    auto* out = static_cast<fftw_complex*>(
        fftw_malloc(sizeof(fftw_complex) * half));
    fftw_plan plan = fftw_plan_dft_r2c_1d(N, buf.data(), out, FFTW_ESTIMATE);
    fftw_execute(plan);

    Aquila::SpectrumType spectrum(half);
    std::vector<double> mag(half);
    for (std::size_t k = 0; k < half; ++k) {
        spectrum[k] = std::complex<double>(out[k][0], out[k][1]);
        mag[k] = std::sqrt(out[k][0] * out[k][0] + out[k][1] * out[k][1]);
    }

    fftw_destroy_plan(plan);
    fftw_free(out);

    return {std::move(spectrum), std::move(mag), half};
}

} // namespace

// ── MFCC fingerprint ───────────────────────────────────────────────────

std::vector<double> MfccAnalyser::compute(const std::vector<double>& block,
                                           const int sample_rate) const {
    if (block.empty()) {
        throw std::invalid_argument("MfccAnalyser::compute: block must not be empty");
    }

    auto [spectrum, mag, half] = runFft(block);

    // Mel filter bank → DCT → MFCCs.
    Aquila::MelFilterBank bank(
        static_cast<Aquila::FrequencyType>(sample_rate),
        static_cast<int>(block.size()));
    const std::vector<double> filter_output = bank.applyAll(spectrum);

    Aquila::Dct dct;
    return dct.dct(filter_output, static_cast<std::size_t>(num_mfcc_));
}

// ── Full analysis (primary + secondary + dominant freq) ────────────────

Fingerprints MfccAnalyser::analyse(const std::vector<double>& block,
                                    const int sample_rate) const {
    if (block.empty()) {
        throw std::invalid_argument("MfccAnalyser::analyse: block must not be empty");
    }

    Fingerprints fp;

    // Single FFT pass — reused for all three outputs.
    auto [spectrum, mag, half] = runFft(block);

    // ── Primary: MFCC coefficients ─────────────────────────────────────
    Aquila::MelFilterBank bank(
        static_cast<Aquila::FrequencyType>(sample_rate),
        static_cast<int>(block.size()));
    const std::vector<double> filter_output = bank.applyAll(spectrum);

    Aquila::Dct dct;
    fp.primary = dct.dct(filter_output, static_cast<std::size_t>(num_mfcc_));

    // ── Secondary: FFT magnitude bins ──────────────────────────────────
    const auto target_bins = static_cast<std::size_t>(
        std::min(num_fft_bins_, static_cast<int>(mag.size())));
    fp.secondary.resize(target_bins);

    if (target_bins >= mag.size()) {
        std::ranges::copy(mag, fp.secondary.begin());
    } else {
        const double ratio = static_cast<double>(mag.size()) /
                             static_cast<double>(target_bins);
        for (std::size_t i = 0; i < target_bins; ++i) {
            const auto di = static_cast<double>(i);
            const auto start = static_cast<std::size_t>(di * ratio);
            const auto end   = static_cast<std::size_t>((di + 1.0) * ratio);
            double sum = 0.0;
            for (std::size_t j = start; j < end && j < mag.size(); ++j) {
                sum += mag[j];
            }
            fp.secondary[i] = sum / static_cast<double>(end - start);
        }
    }

    // ── Dominant frequency ─────────────────────────────────────────────
    if (mag.size() > 1) {
        std::size_t peak_bin = 1;
        double peak_val = mag[1];
        for (std::size_t k = 2; k < mag.size(); ++k) {
            if (mag[k] > peak_val) {
                peak_val = mag[k];
                peak_bin = k;
            }
        }
        fp.dominant_freq = static_cast<double>(peak_bin)
                         * static_cast<double>(sample_rate)
                         / static_cast<double>(block.size());
    }

    return fp;
}

// ── Distance ───────────────────────────────────────────────────────────

double MfccAnalyser::distance(const std::vector<double>& a,
                               const std::vector<double>& b) const {
    double sum = 0.0;
    const std::size_t len = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < len; ++i) {
        const double diff = a[i] - b[i];
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

} // namespace audio::adapter::analysis

