#pragma once

#include "../../domain/Brain.h"
#include "../../domain/SearchContext.h"
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
    [[nodiscard]] std::size_t search(const SearchContext& ctx) const override;
};

}  // namespace audio::adapter::search
