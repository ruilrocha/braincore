#pragma once

#include <vector>

#include "../../domain/port/IFft.h"

namespace audio::adapter::fft {

/**
 * FFTW-based implementation of the IFft port.
 *
 * Wraps FFTW's real-to-complex and complex-to-real transforms,
 * plus a naive DCT-II implementation (since FFTW's DCT requires
 * a separate plan type).
 */
class FftwBackend final : public port::IFft {
public:
    [[nodiscard]] std::vector<ComplexValue> forward(
        std::span<const double> input) const override;

    [[nodiscard]] std::vector<double> inverse(
        std::span<const ComplexValue> input, std::size_t output_size) const override;

    [[nodiscard]] std::vector<double> dct(
        std::span<const double> input, std::size_t output_length) const override;
};

} // namespace audio::adapter::fft
