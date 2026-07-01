#pragma once

#include "../../domain/Brain.h"
#include "../../domain/SearchContext.h"
#include "../../domain/port/ISearchStrategy.h"
#include "SearchUtils.h"

namespace audio::adapter::search {

/**
 * Brute-force closest-match search.
 *
 * Scans every block and picks the one with the smallest full weighted score
 * (multi-feature distance + usage penalty).  Supports stickyness and all
 * per-feature weight parameters from SearchParams.
 *
 * ## Scalability note
 * ClosestSearch is O(N) per block — exact but slow for large corpora.
 * For real-time use with N > ~2000 blocks, prefer VpTreeSearch which provides
 * O(log N) candidate retrieval via the Brain's VP tree index, while producing
 * nearly identical audio quality for most source material.
 *
 * Use ClosestSearch when:
 * - the corpus is small (< ~2000 blocks),
 * - exact matching quality is required, or
 * - `brain.buildIndex()` has not been called.
 */
class ClosestSearch final : public port::ISearchStrategy {
public:
    [[nodiscard]] std::size_t search(const SearchContext& ctx) const override;
};

}  // namespace audio::adapter::search
