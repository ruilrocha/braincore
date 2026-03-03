#pragma once

#include "../../domain/port/ISearchStrategy.h"

namespace audio::adapter::search {

/**
 * Top-N pool search: finds the N closest blocks, then randomly picks one.
 *
 * This provides a middle ground between deterministic closest-match
 * (always the same output for the same input) and fully random search.
 * The pool_size parameter (from SearchParams) controls the trade-off:
 *   - pool_size = 1: identical to ClosestSearch.
 *   - pool_size = 10: pick randomly from the 10 best matches.
 *   - pool_size = 100: very varied output, but still timbral-relevant.
 *
 * Selection within the pool is uniform random (unlike WeightedRandomSearch
 * which uses distance-weighted probabilities over *all* blocks).
 */
class TopNPoolSearch final : public port::ISearchStrategy {
public:
    TopNPoolSearch() = default;

    [[nodiscard]] std::size_t search(
        const std::vector<double>& target_fp,
        std::vector<Block>& blocks,
        const port::IAnalyser& analyser,
        const SearchParams& params,
        std::size_t current_block_index) const override;
};

} // namespace audio::adapter::search

