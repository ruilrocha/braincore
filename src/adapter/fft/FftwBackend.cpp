#include "FftwBackend.h"

#include <cmath>
#include <fftw3.h>

namespace audio::adapter::fft {

std::vector<port::IFft::ComplexValue> FftwBackend::forward(
    std::span<const double> input) const {

    const auto N = static_cast<int>(input.size());
    const auto half = static_cast<std::size_t>(N / 2 + 1);

    // FFTW requires mutable input buffer.
    std::vector<double> buf(input.begin(), input.end());

    auto* out = static_cast<fftw_complex*>(
        fftw_malloc(sizeof(fftw_complex) * half));
    fftw_plan plan = fftw_plan_dft_r2c_1d(N, buf.data(), out, FFTW_ESTIMATE);
    fftw_execute(plan);

    std::vector<ComplexValue> result(half);
    for (std::size_t k = 0; k < half; ++k) {
        result[k] = {out[k][0], out[k][1]};
    }

    fftw_destroy_plan(plan);
    fftw_free(out);
    return result;
}

std::vector<double> FftwBackend::inverse(
    std::span<const ComplexValue> input, std::size_t output_size) const {

    const auto N = static_cast<int>(output_size);
    const auto half = input.size();

    auto* in = static_cast<fftw_complex*>(
        fftw_malloc(sizeof(fftw_complex) * half));
    for (std::size_t k = 0; k < half; ++k) {
        in[k][0] = input[k].real;
        in[k][1] = input[k].imag;
    }

    std::vector<double> result(output_size);
    fftw_plan plan = fftw_plan_dft_c2r_1d(N, in, result.data(), FFTW_ESTIMATE);
    fftw_execute(plan);

    // FFTW inverse is unnormalised — divide by N.
    const double inv_n = 1.0 / static_cast<double>(N);
    for (auto& s : result) s *= inv_n;

    fftw_destroy_plan(plan);
    fftw_free(in);
    return result;
}

std::vector<double> FftwBackend::dct(
    std::span<const double> input, std::size_t output_length) const {

    // DCT-II via naive formula (matches Aquila behaviour).
    const auto N = input.size();
    const auto M = std::min(output_length, N);
    std::vector<double> result(M);

    for (std::size_t k = 0; k < M; ++k) {
        double sum = 0.0;
        for (std::size_t n = 0; n < N; ++n) {
            sum += input[n] * std::cos(
                M_PI * static_cast<double>(k) *
                (2.0 * static_cast<double>(n) + 1.0) /
                (2.0 * static_cast<double>(N)));
        }
        result[k] = sum;
    }
    return result;
}

} // namespace audio::adapter::fft
