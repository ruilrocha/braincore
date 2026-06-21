#include "SynapticSearch.h"

#include "SearchUtils.h"

#include <algorithm>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>

namespace audio::adapter::search {

SynapticSearch::SynapticSearch(const std::size_t num_synapses) : num_synapses_(num_synapses) {}

std::size_t SynapticSearch::search(const SearchContext& ctx) const {
    if (!ctx.brain.hasIndex()) {
        throw std::runtime_error(
            "SynapticSearch: brain index not built — call brain.buildIndex() before playback.");
    }

    const auto& blocks = ctx.brain.blocks();

    const auto& current_mel = blocks[ctx.current_block_index].analysis.print.mel;
    const BlockAnalysis effective = momentum_state_.blend(ctx.target, current_mel, ctx.params);

    const auto neighbours = ctx.brain.neighbors(ctx.current_block_index);
    if (neighbours.empty()) {
        // No precomputed neighbours for this block — fall back to full scan.
        const auto [best_idx, best_score] =
            SearchUtils::scoreCandidates(std::views::iota(std::size_t{0}, blocks.size()), effective,
                                         blocks, ctx.block_usages, ctx.params);
        SearchUtils::applyUsage(ctx.block_usages, best_idx, ctx.params.usage_falloff);
        return SearchUtils::stickify(effective, blocks, ctx.block_usages, best_idx, best_score,
                                     ctx.current_block_index, ctx.params);
    }

    const std::size_t limit = std::min(num_synapses_, neighbours.size());
    const auto [best_idx, best_score] = SearchUtils::scoreCandidates(
        neighbours.first(limit), effective, blocks, ctx.block_usages, ctx.params);

    SearchUtils::applyUsage(ctx.block_usages, best_idx, ctx.params.usage_falloff);
    return SearchUtils::stickify(effective, blocks, ctx.block_usages, best_idx, best_score,
                                 ctx.current_block_index, ctx.params);
}

}  // namespace audio::adapter::search
