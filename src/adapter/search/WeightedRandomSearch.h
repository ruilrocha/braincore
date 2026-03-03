#pragma once

#include "../../domain/port/ISearchStrategy.h"

namespace audio::adapter::search {

/**
 * Weighted-random search: builds a probability distribution inversely
 * proportional to fingerprint distance, then samples from it.
 *
 * Produces varied but semi-coherent output — blocks that are closer to the
 * target are more likely to be selected, but there is randomness injected
 * so the output is never fully deterministic.
 *
 * @p temperature controls the amount of randomness:
 *   - Low temperature (e.g. 0.1): nearly deterministic, close to ClosestSearch.
 *   - High temperature (e.g. 10.0): nearly uniform random.
 */
class WeightedRandomSearch final : public port::ISearchStrategy {
public:
    explicit WeightedRandomSearch(double temperature = 1.0);

    [[nodiscard]] std::size_t search(
        const std::vector<double>& target_fp,
        std::vector<Block>& blocks,
        const port::IAnalyser& analyser,
        const SearchParams& params,
        std::size_t current_block_index) const override;

private:
    double temperature_;
};

} // namespace audio::adapter::search

