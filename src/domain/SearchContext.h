#pragma once

#include "BlockAnalysis.h"
#include "SearchParams.h"

#include <cstddef>
#include <vector>

namespace audio {

class Brain;  // forward declaration

/**
 * Bundles all inputs needed by a search strategy into a single immutable view.
 *
 * Passed by const reference to ISearchStrategy::search().  Grouping the
 * parameters into a struct keeps the search() signature stable as new
 * contextual fields are added in the future — adapters only need to be
 * updated when they actually use a new field.
 *
 * Lifetime: SearchContext holds references; both the struct and all
 * referenced objects must outlive the search() call.  Do not store a
 * SearchContext across calls.
 */
struct SearchContext {
    const Brain& brain;
    const BlockAnalysis& target;
    const SearchParams& params;
    std::size_t current_block_index;
    std::vector<double>& block_usages;  ///< Writable; strategies call applyUsage on it.
};

}  // namespace audio
