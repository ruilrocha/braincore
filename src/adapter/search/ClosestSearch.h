#pragma once

#include "../../domain/port/ISearchStrategy.h"

namespace audio::adapter::search {

/**
 * Brute-force closest-match search.
 *
 * Scans every block and picks the one with the smallest fingerprint distance.
 * Supports stickyness (bias toward the next sequential block for temporal
 * coherence) and usage-based penalties (blocks that have been selected many
 * times receive a distance penalty so other blocks get a chance).
 */
class ClosestSearch final : public port::ISearchStrategy {
public:
    [[nodiscard]] std::size_t search(const std::vector<double>& target_fp,
                                     std::vector<Block>& blocks, const port::IAnalyser& analyser,
                                     const SearchParams& params,
                                     std::size_t current_block_index) const override;
};

}  // namespace audio::adapter::search
