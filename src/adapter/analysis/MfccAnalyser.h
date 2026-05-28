#pragma once

#include "../../aquila/filter/MelFilterBank.h"
#include "../../domain/port/IAnalyser.h"
#include "../../domain/port/IFft.h"

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace audio::adapter::analysis {

/**
 * MFCC + FFT magnitude analyser (adapter implementing the IAnalyser port).
 *
 * Pipeline: IFft::forward() → MelFilterBank (sparse) → IFft::dct().
 *
 * The filter bank is constructed lazily on the first `analyse()` call for a
 * given (sample_rate, block_size) pair and cached for subsequent calls.
 * Since all blocks in a Brain share the same sample_rate and block_size, the
 * construction cost is O(1) per ingestion session rather than O(N).
 */
class MfccAnalyser final : public port::IAnalyser {
public:
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

    // Cached filter bank — rebuilt only when (sample_rate, block_size) changes.
    mutable std::mutex bank_mutex_;
    mutable int cached_sample_rate_ = 0;
    mutable std::size_t cached_block_size_ = 0;
    mutable std::optional<MelFilterBank> bank_cache_;

    const MelFilterBank& filterBank(int sample_rate, std::size_t block_size) const;
};

}  // namespace audio::adapter::analysis
