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

    /**
     * Non-allocating forward FFT: writes output into @p out (size ≥ input.size()/2+1).
     *
     * Writes directly into the caller-provided span via the thread-local scratch
     * complex buffer — no intermediate allocation, no copy.
     */
    void forwardInto(std::span<const double> input, std::span<ComplexValue> out) const override;

    [[nodiscard]] std::vector<double> inverse(std::span<const ComplexValue> input,
                                              std::size_t output_size) const override;

    [[nodiscard]] std::vector<double> dct(std::span<const double> input,
                                          std::size_t output_length) const override;
};

}  // namespace audio::adapter::fft
