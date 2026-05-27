#pragma once

#include "../../domain/Brain.h"
#include "../../domain/port/ISearchStrategy.h"

namespace audio::adapter::search {

/**
 * Brute-force closest-match search.
 *
 * Scans every block and picks the one with the smallest full weighted score
 * (multi-feature distance + usage penalty).  Supports stickyness and all
 * per-feature weight parameters from SearchParams.
 */
class ClosestSearch final : public port::ISearchStrategy {
public:
    [[nodiscard]] std::size_t search(const TargetAnalysis& target, const audio::Brain& brain,
                                     const SearchParams& params, std::size_t current_block_index,
                                     std::vector<double>& block_usages) const override;
};

}  // namespace audio::adapter::search
