#include "FftwSpectralMorph.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include <fftw3.h>

namespace audio::adapter::effects {

std::vector<double> FftwSpectralMorph::apply(
    const std::vector<double>& prev,
    const std::vector<double>& current,
    const double amount) const {

    const auto n = std::min(prev.size(), current.size());
    if (n == 0) return current;
    if (amount <= 0.0) return current;

    const auto N = static_cast<int>(n);
    const auto half = n / 2 + 1;
    const double inv_amount = 1.0 - amount;

    // ── Forward FFT of both blocks ─────────────────────────────────────
    std::vector<double> buf_prev(prev.begin(), prev.begin() + N);
    std::vector<double> buf_curr(current.begin(), current.begin() + N);

    auto* out_prev = static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * half));
    auto* out_curr = static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * half));

    fftw_plan plan_prev = fftw_plan_dft_r2c_1d(N, buf_prev.data(), out_prev, FFTW_ESTIMATE);
    fftw_plan plan_curr = fftw_plan_dft_r2c_1d(N, buf_curr.data(), out_curr, FFTW_ESTIMATE);

    fftw_execute(plan_prev);
    fftw_execute(plan_curr);

    // ── Interpolate magnitudes, blend phases ───────────────────────────
    auto* out_morph = static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * half));

    for (std::size_t k = 0; k < half; ++k) {
        const double mag_prev = std::sqrt(out_prev[k][0] * out_prev[k][0] +
                                          out_prev[k][1] * out_prev[k][1]);
        const double mag_curr = std::sqrt(out_curr[k][0] * out_curr[k][0] +
                                          out_curr[k][1] * out_curr[k][1]);

        // Linear magnitude interpolation.
        const double mag_morph = mag_prev * amount + mag_curr * inv_amount;

        // Phase blending: use weighted circular mean.
        const double phase_prev = std::atan2(out_prev[k][1], out_prev[k][0]);
        const double phase_curr = std::atan2(out_curr[k][1], out_curr[k][0]);

        // Circular interpolation via unit-vector blending.
        const double cx = std::cos(phase_prev) * amount + std::cos(phase_curr) * inv_amount;
        const double cy = std::sin(phase_prev) * amount + std::sin(phase_curr) * inv_amount;
        const double phase_morph = std::atan2(cy, cx);

        out_morph[k][0] = mag_morph * std::cos(phase_morph);
        out_morph[k][1] = mag_morph * std::sin(phase_morph);
    }

    // ── Inverse FFT ────────────────────────────────────────────────────
    std::vector<double> result(n);
    fftw_plan plan_inv = fftw_plan_dft_c2r_1d(N, out_morph, result.data(), FFTW_ESTIMATE);
    fftw_execute(plan_inv);

    // FFTW inverse is unnormalised — divide by N.
    const double inv_n = 1.0 / static_cast<double>(N);
    for (auto& s : result) s *= inv_n;

    // ── RMS matching ───────────────────────────────────────────────────
    // Match output RMS to the blended target RMS to prevent volume drift.
    double rms_prev = 0.0, rms_curr = 0.0, rms_out = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        rms_prev += buf_prev[i] * buf_prev[i];
        rms_curr += buf_curr[i] * buf_curr[i];
        rms_out  += result[i] * result[i];
    }
    const auto dn = static_cast<double>(n);
    rms_prev = std::sqrt(rms_prev / dn);
    rms_curr = std::sqrt(rms_curr / dn);
    rms_out  = std::sqrt(rms_out / dn);

    const double target_rms = rms_prev * amount + rms_curr * inv_amount;
    if (rms_out > 1e-10 && target_rms > 1e-10) {
        const double gain = target_rms / rms_out;
        for (auto& s : result) s *= gain;
    }

    // ── Cleanup ────────────────────────────────────────────────────────
    fftw_destroy_plan(plan_prev);
    fftw_destroy_plan(plan_curr);
    fftw_destroy_plan(plan_inv);
    fftw_free(out_prev);
    fftw_free(out_curr);
    fftw_free(out_morph);

    return result;
}

} // namespace audio::adapter::effects

