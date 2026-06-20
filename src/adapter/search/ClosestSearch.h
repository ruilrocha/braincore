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
 * When SearchParams::momentum > 0, the search target is blended with a
 * predicted position derived from the current block's velocity in MFCC space,
 * causing the output to drift smoothly through the brain's timbral landscape.
 */
class ClosestSearch final : public port::ISearchStrategy {
public:
    [[nodiscard]] std::size_t search(const SearchContext& ctx) const override;

private:
    mutable MomentumState momentum_state_;
};

}  // namespace audio::adapter::search
