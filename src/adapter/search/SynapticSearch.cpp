#include "SynapticSearch.h"

#include "../../domain/port/IAnalyser.h"
#include "SearchUtils.h"

#include <algorithm>
#include <numeric>
#include <span>

namespace audio::adapter::search {

SynapticSearch::SynapticSearch(const std::size_t num_synapses,
                               std::shared_ptr<const SynapseGraph> graph)
    : SynapseAwareSearch(std::move(graph)), num_synapses_(num_synapses) {}

std::size_t SynapticSearch::search(const std::vector<double>& target_fp,
                                   const std::vector<Block>& blocks,
                                   const port::IAnalyser& analyser, const SearchParams& params,
                                   const std::size_t current_block_index,
                                   std::vector<double>& block_usages) const {
    const auto& neighbours = graph_->neighbours;
    if (current_block_index >= neighbours.size() || neighbours[current_block_index].empty()) {
        // Graph exists but current index has no neighbours (e.g. brain was rebuilt
        // with a different block count). Fall through to score the full graph.
        std::vector<std::size_t> all;
        all.reserve(neighbours.size());
        for (std::size_t i = 0; i < neighbours.size(); ++i) {
            all.push_back(i);
        }
        const auto [best_idx, best_score] =
            SearchUtils::scoreCandidates(all, target_fp, blocks, analyser, block_usages, params);
        SearchUtils::applyUsage(block_usages, best_idx, params.usage_falloff);
        return SearchUtils::stickify(target_fp, blocks, analyser, best_idx, best_score,
                                     current_block_index, params.stickyness);
    }

    const auto& candidate_list = neighbours[current_block_index];
    const std::size_t limit = std::min(num_synapses_, candidate_list.size());

    const auto [best_idx, best_score] = SearchUtils::scoreCandidates(
        std::span(candidate_list.data(), limit), target_fp, blocks, analyser, block_usages, params);

    SearchUtils::applyUsage(block_usages, best_idx, params.usage_falloff);
    return SearchUtils::stickify(target_fp, blocks, analyser, best_idx, best_score,
                                 current_block_index, params.stickyness);
}

}  // namespace audio::adapter::search
