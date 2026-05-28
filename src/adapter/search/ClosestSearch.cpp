#include "ClosestSearch.h"

#include "SearchUtils.h"

#include <limits>

namespace audio::adapter::search {

std::size_t ClosestSearch::search(const SearchContext& ctx) const {
    const auto& blocks = ctx.brain.blocks();

    double best_score = std::numeric_limits<double>::max();
    std::size_t best_idx = 0;

    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const double usage = (i < ctx.block_usages.size()) ? ctx.block_usages[i] : 0.0;
        if (const double score =
                SearchUtils::fullScore(ctx.target, blocks[i].analysis, usage, ctx.params);
            score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    SearchUtils::applyUsage(ctx.block_usages, best_idx, ctx.params.usage_falloff);
    return SearchUtils::stickify(ctx.target, blocks, ctx.block_usages, best_idx, best_score,
                                 ctx.current_block_index, ctx.params);
}

}  // namespace audio::adapter::search
