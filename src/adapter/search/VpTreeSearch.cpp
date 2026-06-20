#include "VpTreeSearch.h"

#include "SearchUtils.h"

#include <numeric>
#include <stdexcept>

namespace audio::adapter::search {

VpTreeSearch::VpTreeSearch(const std::size_t num_candidates) : num_candidates_(num_candidates) {}

std::size_t VpTreeSearch::search(const SearchContext& ctx) const {
    if (!ctx.brain.hasIndex()) {
        throw std::runtime_error(
            "VpTreeSearch: brain index not built — call brain.buildIndex() before playback.");
    }

    const auto& blocks = ctx.brain.blocks();

    const auto& current_mfcc = blocks[ctx.current_block_index].analysis.print.mfcc;
    const BlockAnalysis effective = momentum_state_.blend(ctx.target, current_mfcc, ctx.params);

    // O(log N) candidate retrieval using the (possibly momentum-blended) MFCC.
    const auto candidates = ctx.brain.kNearest(effective.print.mfcc, num_candidates_);
    if (candidates.empty()) {
        return 0;
    }

    // Score candidates with the full blended multi-feature distance.
    const auto [best_idx, best_score] =
        SearchUtils::scoreCandidates(candidates, effective, blocks, ctx.block_usages, ctx.params);

    SearchUtils::applyUsage(ctx.block_usages, best_idx, ctx.params.usage_falloff);
    return SearchUtils::stickify(effective, blocks, ctx.block_usages, best_idx, best_score,
                                 ctx.current_block_index, ctx.params);
}

}  // namespace audio::adapter::search
