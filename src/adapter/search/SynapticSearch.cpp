#include "SynapticSearch.h"

#include "SearchUtils.h"

#include <algorithm>
#include <numeric>
#include <span>
#include <stdexcept>

namespace audio::adapter::search {

SynapticSearch::SynapticSearch(const std::size_t num_synapses) : num_synapses_(num_synapses) {}

std::size_t SynapticSearch::search(const std::vector<double>& target_fp, const audio::Brain& brain,
                                   const SearchParams& params,
                                   const std::size_t current_block_index,
                                   std::vector<double>& block_usages) const {
    const auto* idx = brain.index();
    if (idx == nullptr) {
        throw std::runtime_error(
            "SynapticSearch: brain.index() is null — call brain->buildIndex() before playback.");
    }

    const auto& blocks = brain.blocks();
    const auto& analyser = brain.analyser();

    const auto neighbours = idx->neighbors(current_block_index);
    if (neighbours.empty()) {
        // No precomputed neighbours for this block — fall back to full scan.
        std::vector<std::size_t> all(blocks.size());
        std::iota(all.begin(), all.end(), std::size_t{0});
        const auto [best_idx, best_score] =
            SearchUtils::scoreCandidates(all, target_fp, blocks, analyser, block_usages, params);
        SearchUtils::applyUsage(block_usages, best_idx, params.usage_falloff);
        return SearchUtils::stickify(target_fp, blocks, analyser, best_idx, best_score,
                                     current_block_index, params.stickyness);
    }

    const std::size_t limit = std::min(num_synapses_, neighbours.size());
    const auto [best_idx, best_score] = SearchUtils::scoreCandidates(
        neighbours.first(limit), target_fp, blocks, analyser, block_usages, params);

    SearchUtils::applyUsage(block_usages, best_idx, params.usage_falloff);
    return SearchUtils::stickify(target_fp, blocks, analyser, best_idx, best_score,
                                 current_block_index, params.stickyness);
}

}  // namespace audio::adapter::search
