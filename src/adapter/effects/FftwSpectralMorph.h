#pragma once

#include "../../domain/port/IBlockEffect.h"

namespace audio::adapter::effects {

/**
 * Spectral morphing via FFTW: interpolates magnitudes and blends phases
 * in the frequency domain for smooth timbral transitions between blocks.
 *
 * This is far superior to sample-domain cross-fading because it preserves
 * harmonic structure while morphing timbre — the result sounds like one
 * sound transforming into another rather than two sounds overlapping.
 */
class FftwSpectralMorph final : public port::IBlockEffect {
public:
    [[nodiscard]] std::vector<double> apply(
        const std::vector<double>& prev,
        const std::vector<double>& current,
        double amount) const override;
};

} // namespace audio::adapter::effects
