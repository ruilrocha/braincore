#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace audio::port {

/**
 * Port: FFT/DCT computation abstraction.
 *
 * Allows the domain and use-case layers to remain agnostic about the
 * concrete FFT library in use (FFTW, PocketFFT, etc.).
 *
 * Adapters live in src/adapter/fft/.
 */
class IFft {
public:
    virtual ~IFft() = default;

    struct ComplexValue {
        double real = 0.0;
        double imag = 0.0;
    };

    /**
     * Real-to-complex forward FFT.
     *
     * @param input  Real-valued time-domain samples.
     * @return       Complex spectrum of size (input.size() / 2 + 1).
     */
    [[nodiscard]] virtual std::vector<ComplexValue> forward(
        std::span<const double> input) const = 0;

    /**
     * Complex-to-real inverse FFT.
     *
     * @param input       Complex spectrum (from forward()).
     * @param output_size Expected number of real output samples.
     * @return            Real-valued time-domain samples (normalised by 1/N).
     */
    [[nodiscard]] virtual std::vector<double> inverse(
        std::span<const ComplexValue> input, std::size_t output_size) const = 0;

    /**
     * Discrete Cosine Transform (type-II).
     *
     * @param input         Input data.
     * @param output_length Number of DCT coefficients to return.
     * @return              DCT coefficients (first output_length values).
     */
    [[nodiscard]] virtual std::vector<double> dct(
        std::span<const double> input, std::size_t output_length) const = 0;
};

} // namespace audio::port
