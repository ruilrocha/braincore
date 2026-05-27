#pragma once

#include "../../domain/Brain.h"
#include "../../domain/port/ISearchStrategy.h"

#include <cstddef>

namespace audio::adapter::search {

/**
 * O(log N) nearest-neighbour search using the Brain's VP tree index.
 *
 * For each target fingerprint, queries the nearest-neighbour index
 * (`brain.index()->kNearest(target_fp, k)`) to obtain a small candidate
 * set, then scores each candidate with `SearchUtils::fullScore()` (which
 * applies the same per-feature weights / n_ratio / usage penalty logic
 * as ClosestSearch) and returns the best one.
 *
 * Expected complexity: O(K·F + log N·F) vs. ClosestSearch's O(N·F),
 * where F is the fingerprint dimension.  For large brains (N ≫ K) this is
 * significantly faster while producing nearly identical quality results.
 *
 * Requires `brain->buildIndex()` to have been called before playback.
 * Throws `std::runtime_error` if the index is absent.
 *
 * @p num_candidates  How many VP tree candidates to evaluate per call.
 *                    Higher values → slower but closer to ClosestSearch quality.
 */
class VpTreeSearch final : public port::ISearchStrategy {
public:
    explicit VpTreeSearch(std::size_t num_candidates = 32);

    [[nodiscard]] std::size_t search(const TargetAnalysis& target, const Brain& brain,
                                     const SearchParams& params, std::size_t current_block_index,
                                     std::vector<double>& block_usages) const override;

private:
    std::size_t num_candidates_;
};

}  // namespace audio::adapter::search
