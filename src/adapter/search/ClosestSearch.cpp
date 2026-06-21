#include "ClosestSearch.h"

#include "SearchUtils.h"

#include <limits>

namespace audio::adapter::search {

std::size_t ClosestSearch::search(const SearchContext& ctx) const {
    const auto& blocks = ctx.brain.blocks();

    const auto& current_mel = blocks[ctx.current_block_index].analysis.print.mel;
    const BlockAnalysis effective = momentum_state_.blend(ctx.target, current_mel, ctx.params);

    double best_score = std::numeric_limits<double>::max();
    std::size_t best_idx = 0;

    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const double usage = (i < ctx.block_usages.size()) ? ctx.block_usages[i] : 0.0;
        if (const double score =
                SearchUtils::fullScore(effective, blocks[i].analysis, usage, ctx.params);
            score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    SearchUtils::applyUsage(ctx.block_usages, best_idx, ctx.params.usage_falloff);
    return SearchUtils::stickify(effective, blocks, ctx.block_usages, best_idx, best_score,
                                 ctx.current_block_index, ctx.params);
}

}  // namespace audio::adapter::search
