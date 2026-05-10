#include "PocketfftBackend.h"

#include <cmath>
#include <cstring>

#include "../../../third_party/pocketfft/pocketfft_hdronly.h"

namespace audio::adapter::fft {

std::vector<port::IFft::ComplexValue> PocketfftBackend::forward(
    std::span<const double> input) const {

    const auto N = input.size();
    const auto half = N / 2 + 1;

    pocketfft::shape_t shape{N};
    pocketfft::stride_t stride_in{sizeof(double)};
    pocketfft::stride_t stride_out{sizeof(std::complex<double>)};
    pocketfft::shape_t axes{0};

    std::vector<std::complex<double>> out(half);

    pocketfft::r2c(shape, stride_in, stride_out, axes,
                   pocketfft::FORWARD, input.data(), out.data(), 1.0);

    std::vector<ComplexValue> result(half);
    for (std::size_t k = 0; k < half; ++k) {
        result[k] = {out[k].real(), out[k].imag()};
    }
    return result;
}

std::vector<double> PocketfftBackend::inverse(
    std::span<const ComplexValue> input, std::size_t output_size) const {

    const auto half = input.size();

    pocketfft::shape_t shape{output_size};
    pocketfft::stride_t stride_in{sizeof(std::complex<double>)};
    pocketfft::stride_t stride_out{sizeof(double)};
    pocketfft::shape_t axes{0};

    std::vector<std::complex<double>> in(half);
    for (std::size_t k = 0; k < half; ++k) {
        in[k] = std::complex<double>(input[k].real, input[k].imag);
    }

    std::vector<double> result(output_size);

    // PocketFFT c2r normalisation: pass 1/N as scale factor.
    const double scale = 1.0 / static_cast<double>(output_size);
    pocketfft::c2r(shape, stride_in, stride_out, axes,
                   pocketfft::BACKWARD, in.data(), result.data(), scale);

    return result;
}

std::vector<double> PocketfftBackend::dct(
    std::span<const double> input, std::size_t output_length) const {

    const auto N = input.size();

    pocketfft::shape_t shape{N};
    pocketfft::stride_t stride{sizeof(double)};
    pocketfft::shape_t axes{0};

    // PocketFFT DCT type-II.
    std::vector<double> buf(input.begin(), input.end());
    pocketfft::dct(shape, stride, stride, axes, 2 /* type-II */,
                   buf.data(), buf.data(), 1.0, true /* ortho */);

    // Return only the first output_length coefficients.
    const auto M = std::min(output_length, N);
    buf.resize(M);
    return buf;
}

} // namespace audio::adapter::fft
