#pragma once

#include "../../domain/Brain.h"
#include "../../domain/SearchContext.h"
#include "../../domain/port/ISearchStrategy.h"
#include "SearchUtils.h"

#include <cstddef>

namespace audio::adapter::search {

/**
 * Graph-based "synaptic" search.
 *
 * Restricts candidate selection to the precomputed nearest-neighbours of the
 * current block (via `brain.neighbors(i)`), producing output that evolves
 * smoothly through the brain's timbral space.
 *
 * Requires `brain.buildIndex()` to have been called before playback.
 * Throws `std::runtime_error` if the index is absent.
 *
 * @p num_synapses controls how many precomputed neighbours are evaluated per
 * call (capped at the actual number stored in the index).
 */
class SynapticSearch final : public port::ISearchStrategy {
public:
    explicit SynapticSearch(std::size_t num_synapses = 100);

    [[nodiscard]] std::size_t search(const SearchContext& ctx) const override;

private:
    std::size_t num_synapses_;
};

}  // namespace audio::adapter::search
