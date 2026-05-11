#pragma once

#include "SynapseAwareSearch.h"

#include <cstddef>
#include <memory>

namespace audio::adapter::search {

/**
 * Graph-based "synaptic" search.
 *
 * Applies closest-match scoring restricted to the pre-computed nearest-neighbours
 * of the *current* block, producing output that evolves smoothly through the
 * brain's timbral space.
 *
 * The SynapseGraph is injected at construction time and must be non-null —
 * construction throws `std::invalid_argument` otherwise.
 *
 * @p num_synapses controls how many neighbours are evaluated per call.
 */
class SynapticSearch final : public SynapseAwareSearch {
public:
    explicit SynapticSearch(std::size_t num_synapses, std::shared_ptr<const SynapseGraph> graph);

    [[nodiscard]] std::size_t search(const std::vector<double>& target_fp,
                                     const std::vector<Block>& blocks,
                                     const port::IAnalyser& analyser, const SearchParams& params,
                                     std::size_t current_block_index,
                                     std::vector<double>& block_usages) const override;

private:
    std::size_t num_synapses_;
};

}  // namespace audio::adapter::search
