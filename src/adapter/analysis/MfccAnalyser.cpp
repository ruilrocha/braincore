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

// ── Filter bank cache ──────────────────────────────────────────────────────────

const MelFilterBank& MfccAnalyser::filterBank(const int sample_rate,
                                              const std::size_t block_size) const {
    std::scoped_lock lock(bank_mutex_);
    if (!bank_cache_.has_value() || cached_sample_rate_ != sample_rate ||
        cached_block_size_ != block_size) {
        bank_cache_.emplace(static_cast<double>(sample_rate), block_size);
        cached_sample_rate_ = sample_rate;
        cached_block_size_ = block_size;
    }
    return *bank_cache_;
}

// ── Internal helpers ───────────────────────────────────────────────────────────

namespace {

struct FftResult {
    std::vector<double> magnitude;
    std::size_t half_n;
};

FftResult runFft(const port::IFft& fft, const std::vector<double>& block) {
    const auto half = (block.size() / 2) + 1;
    const auto complex_out = fft.forward(block);

    std::vector<double> mag(half);
    for (std::size_t k = 0; k < half; ++k) {
        mag[k] = std::sqrt((complex_out[k].real * complex_out[k].real) +
                           (complex_out[k].imag * complex_out[k].imag));
    }

    return {.magnitude = std::move(mag), .half_n = half};
}

std::vector<double> chromaFromMag(const std::vector<double>& mag, std::size_t fft_size,
                                  int sample_rate) {
    std::vector<double> chroma(12, 0.0);
    const double df = static_cast<double>(sample_rate) / static_cast<double>(fft_size);

    for (std::size_t k = 1; k < mag.size(); ++k) {
        const double freq = static_cast<double>(k) * df;
        if (freq < 1.0) {
            continue;
        }
        const double midi = (12.0 * std::log2(freq / 440.0)) + 69.0;
        int pc = static_cast<int>(std::round(midi)) % 12;
        if (pc < 0)
            pc += 12;
        chroma[static_cast<std::size_t>(pc)] += mag[k] * mag[k];
    }

    double sum = 0.0;
    for (const double energy : chroma) {
        sum += energy;
    }
    if (sum > 1e-12) {
        for (double& energy : chroma) {
            energy /= sum;
        }
    }
    return chroma;
}

}  // namespace

// ── MFCC fingerprint ───────────────────────────────────────────────────

std::vector<double> MfccAnalyser::compute(const std::vector<double>& block,
                                          const int sample_rate) const {
    if (block.empty()) {
        throw std::invalid_argument("MfccAnalyser::compute: block must not be empty");
    }
    auto [mag, half] = runFft(*fft_, block);
    const auto& bank = filterBank(sample_rate, block.size());
    const auto filter_output = bank.apply(mag);
    return fft_->dct(filter_output, static_cast<std::size_t>(num_mfcc_));
}

// ── Full analysis ──────────────────────────────────────────────────────

AudioPrint MfccAnalyser::analyse(const std::vector<double>& block, const int sample_rate) const {
    if (block.empty()) {
        throw std::invalid_argument("MfccAnalyser::analyse: block must not be empty");
    }

    AudioPrint fp;

    auto [mag, half] = runFft(*fft_, block);

    // ── Primary: MFCC (via cached filter bank) ─────────────────────────
    const auto& bank = filterBank(sample_rate, block.size());
    const auto filter_output = bank.apply(mag);
    fp.mfcc = fft_->dct(filter_output, static_cast<std::size_t>(num_mfcc_));

    // ── Mel filter-bank log energies ───────────────────────────────────
    fp.mel = filter_output;

    // ── Spectral: FFT magnitude bins ───────────────────────────────────
    const auto target_bins =
        static_cast<std::size_t>(std::min(num_fft_bins_, static_cast<int>(mag.size())));
    fp.spectral.resize(target_bins);
    if (target_bins >= mag.size()) {
        std::copy(mag.begin(), mag.end(), fp.spectral.begin());
    } else {
        const double ratio = static_cast<double>(mag.size()) / static_cast<double>(target_bins);
        for (std::size_t i = 0; i < target_bins; ++i) {
            const auto start = static_cast<std::size_t>(static_cast<double>(i) * ratio);
            const auto end = static_cast<std::size_t>(static_cast<double>(i + 1) * ratio);
            double sum = 0.0;
            const std::size_t count = end > start ? end - start : 1;
            for (std::size_t j = start; j < end && j < mag.size(); ++j) {
                sum += mag[j];
            }
            fp.spectral[i] = sum / static_cast<double>(count);
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

    // ── Chroma (free — reuses mag already computed) ────────────────────
    fp.chroma = chromaFromMag(mag, block.size(), sample_rate);

    return fp;
}

// ── Distance ───────────────────────────────────────────────────────────

double MfccAnalyser::distance(const std::vector<double>& fp_a,
                              const std::vector<double>& fp_b) const {
    double sum = 0.0;
    const std::size_t len = std::min(fp_a.size(), fp_b.size());
    for (std::size_t i = 0; i < len; ++i) {
        const double diff = fp_a[i] - fp_b[i];
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

}  // namespace audio::adapter::analysis
