#pragma once

#include <vector>

namespace audio::port {

/**
 * Port: block-pair effect processing.
 *
 * Operates on two consecutive matched blocks (previous and current)
 * to produce a smooth transition between them.  The canonical use
 * case is spectral morphing, but any effect that needs two blocks
 * for context (cross-synthesis, convolution blend, etc.) can implement
 * this interface.
 *
 * Lives in the domain layer.  Concrete implementations that depend on
 * external libraries (e.g. FFTW) live in src/adapter/effects/.
 */
class IBlockEffect {
public:
    virtual ~IBlockEffect() = default;

    /**
     * Apply the effect to produce a blended output block.
     *
     * @param prev    Samples from the previous matched block.
     * @param current Samples from the current matched block.
     * @param amount  Effect intensity [0.0, 1.0].
     *                0.0 = pass current through unchanged.
     *                1.0 = full effect.
     * @return        Processed output samples (same length as current).
     */
    [[nodiscard]] virtual std::vector<double> apply(
        const std::vector<double>& prev,
        const std::vector<double>& current,
        double amount) const = 0;
};

} // namespace audio::port

