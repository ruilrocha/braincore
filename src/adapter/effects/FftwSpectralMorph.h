#pragma once

#include "../../domain/port/IBlockEffect.h"
#include "../../domain/port/IFft.h"

#include <memory>

namespace audio::adapter::effects {

/**
 * Spectral morphing: interpolates magnitudes and blends phases in the
 * frequency domain for smooth timbral transitions between blocks.
 *
 * Uses the injected IFft backend (PocketFFT or FFTW).
 */
class SpectralMorph final : public port::IBlockEffect {
public:
    explicit SpectralMorph(std::shared_ptr<port::IFft> fft);

    [[nodiscard]] std::vector<double> apply(const std::vector<double>& prev,
                                            const std::vector<double>& current,
                                            double amount) const override;

private:
    std::shared_ptr<port::IFft> fft_;
};

// Keep old name as alias for backward compatibility.
using FftwSpectralMorph = SpectralMorph;

}  // namespace audio::adapter::effects
