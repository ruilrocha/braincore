#include "MfccAnalyser.h"

#include "../../aquila/filter/MelFilterBank.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace audio::adapter::analysis {

MfccAnalyser::MfccAnalyser(std::shared_ptr<port::IFft> fft, const int num_mfcc,
                           const int num_fft_bins)
    : fft_(std::move(fft)), num_mfcc_(num_mfcc), num_fft_bins_(num_fft_bins) {}

// ── Internal helper: run forward FFT and extract magnitude ────────────────────

namespace {

struct FftResult {
    std::vector<double> magnitude;
    std::size_t half_n;
};

FftResult runFft(const port::IFft& fft, const std::vector<double>& block) {
    const auto half = (block.size() / 2) + 1;
    auto complex_out = fft.forward(block);

    std::vector<double> mag(half);
    for (std::size_t k = 0; k < half; ++k) {
        mag[k] = std::sqrt((complex_out[k].real * complex_out[k].real) +
                           (complex_out[k].imag * complex_out[k].imag));
    }

    return {.magnitude = std::move(mag), .half_n = half};
}

}  // namespace

// ── MFCC fingerprint ───────────────────────────────────────────────────

std::vector<double> MfccAnalyser::compute(const std::vector<double>& block,
                                          const int sample_rate) const {
    if (block.empty()) {
        throw std::invalid_argument("MfccAnalyser::compute: block must not be empty");
    }

    auto [mag, half] = runFft(*fft_, block);

    // Mel filter bank (sparse) → DCT → MFCCs.
    const MelFilterBank bank(sample_rate, block.size());
    const auto filter_output = bank.apply(mag);

    return fft_->dct(filter_output, static_cast<std::size_t>(num_mfcc_));
}

// ── Full analysis (primary + secondary + dominant freq) ────────────────

AudioPrint MfccAnalyser::analyse(const std::vector<double>& block, const int sample_rate) const {
    if (block.empty()) {
        throw std::invalid_argument("MfccAnalyser::analyse: block must not be empty");
    }

    AudioPrint fp;

    // Single FFT pass — magnitude computed once, reused for all outputs.
    auto [mag, half] = runFft(*fft_, block);

    // ── Primary: MFCC coefficients ─────────────────────────────────────
    MelFilterBank bank(sample_rate, block.size());
    const auto filter_output = bank.apply(mag);

    fp.mfcc = fft_->dct(filter_output, static_cast<std::size_t>(num_mfcc_));

    // ── Secondary: FFT magnitude bins ──────────────────────────────────
    const auto target_bins =
        static_cast<std::size_t>(std::min(num_fft_bins_, static_cast<int>(mag.size())));
    fp.spectral.resize(target_bins);

    if (target_bins >= mag.size()) {
        std::ranges::copy(mag, fp.spectral.begin());
    } else {
        const double ratio = static_cast<double>(mag.size()) / static_cast<double>(target_bins);
        for (std::size_t i = 0; i < target_bins; ++i) {
            const auto di = static_cast<double>(i);
            const auto start = static_cast<std::size_t>(di * ratio);
            const auto end = static_cast<std::size_t>((di + 1.0) * ratio);
            double sum = 0.0;
            for (std::size_t j = start; j < end && j < mag.size(); ++j) {
                sum += mag[j];
            }
            fp.spectral[i] = sum / static_cast<double>(end - start);
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
        fp.dominant_freq = static_cast<double>(peak_bin) * static_cast<double>(sample_rate) /
                           static_cast<double>(block.size());
    }

    return fp;
}

// ── Distance ───────────────────────────────────────────────────────────

double MfccAnalyser::distance(const std::vector<double>& a, const std::vector<double>& b) const {
    double sum = 0.0;
    const std::size_t len = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < len; ++i) {
        const double diff = a[i] - b[i];
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

}  // namespace audio::adapter::analysis
