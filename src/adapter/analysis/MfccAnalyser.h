#pragma once

#include <memory>
#include <vector>

#include "../../domain/port/IAnalyser.h"
#include "../../domain/port/IFft.h"

namespace audio::adapter::analysis {

/**
 * MFCC + FFT magnitude analyser (adapter implementing the IAnalyser port).
 *
 * Pipeline: IFft::forward() → Aquila MelFilterBank → IFft::dct().
 *
 * The primary fingerprint is MFCC coefficients (timbral envelope).
 * The secondary fingerprint is FFT magnitude bins (spectral detail).
 * These two can be blended via SearchParams::blend_ratio.
 */
class MfccAnalyser final : public port::IAnalyser {
public:
    /**
     * @param fft          Injected FFT backend (PocketFFT or FFTW).
     * @param num_mfcc     Number of MFCC coefficients (primary fingerprint size).
     * @param num_fft_bins Number of FFT magnitude bins (secondary fingerprint size).
     */
    explicit MfccAnalyser(std::shared_ptr<port::IFft> fft,
                          int num_mfcc = 12, int num_fft_bins = 100);

    [[nodiscard]] std::vector<double> compute(
        const std::vector<double>& block, int sample_rate) const override;

    [[nodiscard]] Fingerprints analyse(
        const std::vector<double>& block, int sample_rate) const override;

    [[nodiscard]] double distance(
        const std::vector<double>& a,
        const std::vector<double>& b) const override;

private:
    std::shared_ptr<port::IFft> fft_;
    int num_mfcc_;
    int num_fft_bins_;
};

} // namespace audio::adapter::analysis
