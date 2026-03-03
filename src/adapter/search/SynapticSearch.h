#pragma once

#include <cstddef>
#include "../../domain/port/ISearchStrategy.h"

namespace audio::adapter::search {

/**
 * Graph-based "synaptic" search.
 *
 * Instead of scanning every block in the brain, this strategy only looks at
 * the pre-computed nearest-neighbours (synapses) of the *current* block.
 * This produces outputs that evolve smoothly through the brain's timbral
 * space rather than jumping wildly — a form of constrained random walk.
 *
 * Requires Brain::buildSynapses() to have been called first.
 *
 * @p num_synapses controls how many of the current block's synapses are
 * evaluated (the list is ordered by closeness, so fewer synapses = more
 * constrained / more coherent output).
 */
class SynapticSearch final : public port::ISearchStrategy {
public:
    /**
     * @param num_synapses Maximum number of synapses to evaluate per call.
     */
    explicit SynapticSearch(std::size_t num_synapses = 100);

    [[nodiscard]] std::size_t search(
        const std::vector<double>& target_fp,
        std::vector<Block>& blocks,
        const port::IAnalyser& analyser,
        const SearchParams& params,
        std::size_t current_block_index) const override;

private:
    std::size_t num_synapses_;
};

} // namespace audio::adapter::search

