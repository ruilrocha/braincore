#pragma once

#include "../../domain/port/IAnalyser.h"
#include "../../domain/port/IFft.h"

#include <memory>
#include <vector>

namespace audio::adapter::analysis {

/**
 * MFCC + FFT magnitude analyser (adapter implementing the IAnalyser port).
 *
 * Pipeline: IFft::forward() → MelFilterBank (sparse) → IFft::dct().
 *
 * The primary AudioPrint is MFCC coefficients (timbral envelope).
 * The secondary AudioPrint is FFT magnitude bins (spectral detail).
 * Chroma (pitch-class) and F0 are also computed — see AudioPrint.
 * Matching weights are controlled via SearchParams (mfcc_weight, spectral_weight, etc.).
 */
class MfccAnalyser final : public port::IAnalyser {
public:
    /**
     * @param fft          Injected FFT backend (PocketFFT or FFTW).
     * @param num_mfcc     Number of MFCC coefficients (primary fingerprint size).
     * @param num_fft_bins Number of FFT magnitude bins (secondary fingerprint size).
     */
    explicit MfccAnalyser(std::shared_ptr<port::IFft> fft, int num_mfcc = 12,
                          int num_fft_bins = 100);

    [[nodiscard]] std::vector<double> compute(const std::vector<double>& block,
                                              int sample_rate) const override;

    [[nodiscard]] AudioPrint analyse(const std::vector<double>& block,
                                     int sample_rate) const override;

    [[nodiscard]] double distance(const std::vector<double>& a,
                                  const std::vector<double>& b) const override;

private:
    std::shared_ptr<port::IFft> fft_;
    int num_mfcc_;
    int num_fft_bins_;
};

}  // namespace audio::adapter::analysis
