#include "VpTreeSearch.h"

#include "SearchUtils.h"

#include <numeric>
#include <stdexcept>

namespace audio::adapter::search {

VpTreeSearch::VpTreeSearch(const std::size_t num_candidates) : num_candidates_(num_candidates) {}

std::size_t VpTreeSearch::search(const std::vector<double>& target_fp, const Brain& brain,
                                 const SearchParams& params, const std::size_t current_block_index,
                                 std::vector<double>& block_usages) const {
    const auto* idx = brain.index();
    if (idx == nullptr) {
        throw std::runtime_error(
            "VpTreeSearch: brain.index() is null — call brain->buildIndex() before playback.");
    }

    const auto& blocks = brain.blocks();
    const auto& analyser = brain.analyser();

    // O(log N) candidate retrieval.
    auto candidates = idx->kNearest(target_fp, num_candidates_);
    if (candidates.empty()) {
        // Degenerate case: fall back to block 0.
        return 0;
    }

    // Score candidates with the full blended distance (same as ClosestSearch).
    // Build a synthetic target block so we can reuse fullScore().
    audio::Block target_block;
    target_block.print.mfcc = target_fp;

    double best_score = std::numeric_limits<double>::max();
    std::size_t best_idx = candidates[0];

    for (const std::size_t cand_idx : candidates) {
        if (cand_idx >= blocks.size()) {
            continue;
        }
        const double score =
            SearchUtils::fullScore(target_block, blocks[cand_idx], block_usages, cand_idx, params);
        if (score < best_score) {
            best_score = score;
            best_idx = cand_idx;
        }
    }

    SearchUtils::applyUsage(block_usages, best_idx, params.usage_falloff);

    return SearchUtils::stickify(target_fp, blocks, analyser, best_idx, best_score,
                                 current_block_index, params.stickyness);
}

}  // namespace audio::adapter::search
