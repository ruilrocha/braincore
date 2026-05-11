#pragma once

#include "../../domain/port/IFft.h"

#include <vector>

namespace audio::adapter::fft {

/**
 * PocketFFT-based implementation of the IFft port.
 *
 * Header-only library — no external linking required.
 * Provides FFT (r2c/c2r) and DCT-II natively.
 */
class PocketfftBackend final : public port::IFft {
public:
    [[nodiscard]] std::vector<ComplexValue> forward(std::span<const double> input) const override;

    [[nodiscard]] std::vector<double> inverse(std::span<const ComplexValue> input,
                                              std::size_t output_size) const override;

    [[nodiscard]] std::vector<double> dct(std::span<const double> input,
                                          std::size_t output_length) const override;
};

}  // namespace audio::adapter::fft
