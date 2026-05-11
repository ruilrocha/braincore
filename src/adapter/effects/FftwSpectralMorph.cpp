#include "FftwSpectralMorph.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace audio::adapter::effects {

SpectralMorph::SpectralMorph(std::shared_ptr<port::IFft> fft) : fft_(std::move(fft)) {}

std::vector<double> SpectralMorph::apply(const std::vector<double>& prev,
                                         const std::vector<double>& current,
                                         const double amount) const {
    const auto n = std::min(prev.size(), current.size());
    if (n == 0) {
        return current;
    }
    if (amount <= 0.0) {
        return current;
    }

    const double inv_amount = 1.0 - amount;

    // ── Forward FFT of both blocks ─────────────────────────────────────
    const std::span prev_span(prev.data(), n);
    const std::span curr_span(current.data(), n);

    auto spec_prev = fft_->forward(prev_span);
    auto spec_curr = fft_->forward(curr_span);

    const auto half = spec_prev.size();

    // ── Interpolate magnitudes, blend phases ───────────────────────────
    std::vector<port::IFft::ComplexValue> morphed(half);

    for (std::size_t k = 0; k < half; ++k) {
        const double mag_prev = std::sqrt((spec_prev[k].real * spec_prev[k].real) +
                                          (spec_prev[k].imag * spec_prev[k].imag));
        const double mag_curr = std::sqrt((spec_curr[k].real * spec_curr[k].real) +
                                          (spec_curr[k].imag * spec_curr[k].imag));

        const double mag_morph = (mag_prev * amount) + (mag_curr * inv_amount);

        const double phase_prev = std::atan2(spec_prev[k].imag, spec_prev[k].real);
        const double phase_curr = std::atan2(spec_curr[k].imag, spec_curr[k].real);

        // Circular interpolation via unit-vector blending.
        const double cx = (std::cos(phase_prev) * amount) + (std::cos(phase_curr) * inv_amount);
        const double cy = (std::sin(phase_prev) * amount) + (std::sin(phase_curr) * inv_amount);
        const double phase_morph = std::atan2(cy, cx);

        morphed[k] = {.real = mag_morph * std::cos(phase_morph),
                      .imag = mag_morph * std::sin(phase_morph)};
    }

    // ── Inverse FFT ────────────────────────────────────────────────────
    auto result = fft_->inverse(morphed, n);

    // ── RMS matching ───────────────────────────────────────────────────
    double rms_prev = 0.0;
    double rms_curr = 0.0;
    double rms_out = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        rms_prev += prev[i] * prev[i];
        rms_curr += current[i] * current[i];
        rms_out += result[i] * result[i];
    }
    const auto dn = static_cast<double>(n);
    rms_prev = std::sqrt(rms_prev / dn);
    rms_curr = std::sqrt(rms_curr / dn);
    rms_out = std::sqrt(rms_out / dn);

    const double target_rms = (rms_prev * amount) + (rms_curr * inv_amount);
    if (rms_out > 1e-10 && target_rms > 1e-10) {
        const double gain = target_rms / rms_out;
        for (auto& s : result) {
            s *= gain;
        }
    }

    return result;
}

}  // namespace audio::adapter::effects
