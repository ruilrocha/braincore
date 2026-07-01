#include "PocketfftBackend.h"

#include <cmath>
#include <cstring>
#include <pocketfft_hdronly.h>

namespace audio::adapter::fft {

// Thread-local scratch buffers — reused across calls to avoid per-call heap
// allocation on the audio thread. Each thread gets its own copy, so no locking.
namespace {

struct FftScratch {
    std::vector<std::complex<double>> complex_buf;
    std::vector<double> real_buf;
};

FftScratch& scratch() {
    thread_local FftScratch s;
    return s;
}

}  // namespace

std::vector<port::IFft::ComplexValue> PocketfftBackend::forward(
    std::span<const double> input) const {
    const auto num_input = input.size();
    const auto half = (num_input / 2) + 1;

    const pocketfft::shape_t shape{num_input};
    const pocketfft::stride_t stride_in{sizeof(double)};
    const pocketfft::stride_t stride_out{sizeof(std::complex<double>)};
    const pocketfft::shape_t axes{0};

    auto& sc = scratch();
    sc.complex_buf.resize(half);

    pocketfft::r2c(shape, stride_in, stride_out, axes, pocketfft::FORWARD, input.data(),
                   sc.complex_buf.data(), 1.0);

    std::vector<ComplexValue> result(half);
    for (std::size_t k = 0; k < half; ++k) {
        result[k] = {.real = sc.complex_buf[k].real(), .imag = sc.complex_buf[k].imag()};
    }
    return result;
}

void PocketfftBackend::forwardInto(std::span<const double> input,
                                   std::span<ComplexValue> out) const {
    const auto num_input = input.size();
    const auto half = (num_input / 2) + 1;

    const pocketfft::shape_t shape{num_input};
    const pocketfft::stride_t stride_in{sizeof(double)};
    const pocketfft::stride_t stride_out{sizeof(std::complex<double>)};
    const pocketfft::shape_t axes{0};

    auto& sc = scratch();
    sc.complex_buf.resize(half);

    pocketfft::r2c(shape, stride_in, stride_out, axes, pocketfft::FORWARD, input.data(),
                   sc.complex_buf.data(), 1.0);

    // Write directly into caller buffer — no separate result vector.
    const std::size_t n = std::min(half, out.size());
    for (std::size_t k = 0; k < n; ++k) {
        out[k] = {.real = sc.complex_buf[k].real(), .imag = sc.complex_buf[k].imag()};
    }
}

std::vector<double> PocketfftBackend::inverse(std::span<const ComplexValue> input,
                                              std::size_t output_size) const {
    const auto half = input.size();

    pocketfft::shape_t shape{output_size};
    pocketfft::stride_t stride_in{sizeof(std::complex<double>)};
    pocketfft::stride_t stride_out{sizeof(double)};
    pocketfft::shape_t axes{0};

    // Reuse thread-local buffers.
    auto& sc = scratch();
    sc.complex_buf.resize(half);
    for (std::size_t k = 0; k < half; ++k) {
        sc.complex_buf[k] = std::complex(input[k].real, input[k].imag);
    }

    sc.real_buf.resize(output_size);

    const double scale = 1.0 / static_cast<double>(output_size);
    pocketfft::c2r(shape, stride_in, stride_out, axes, pocketfft::BACKWARD, sc.complex_buf.data(),
                   sc.real_buf.data(), scale);

    return {sc.real_buf.begin(), sc.real_buf.end()};
}

std::vector<double> PocketfftBackend::dct(std::span<const double> input,
                                          std::size_t output_length) const {
    const auto num_input = input.size();

    pocketfft::shape_t shape{num_input};
    pocketfft::stride_t stride{sizeof(double)};
    pocketfft::shape_t axes{0};

    // Reuse thread-local real buffer for in-place DCT.
    auto& sc = scratch();
    sc.real_buf.assign(input.begin(), input.end());
    pocketfft::dct(shape, stride, stride, axes, 2 /* type-II */, sc.real_buf.data(),
                   sc.real_buf.data(), 1.0, true /* ortho */);

    const auto num_coeffs = std::min(output_length, num_input);
    return {sc.real_buf.begin(), sc.real_buf.begin() + static_cast<std::ptrdiff_t>(num_coeffs)};
}

}  // namespace audio::adapter::fft
