#include "VpTreeSearch.h"

#include "SearchUtils.h"

#include <numeric>
#include <stdexcept>

namespace audio::adapter::search {

VpTreeSearch::VpTreeSearch(const std::size_t num_candidates) : num_candidates_(num_candidates) {}

std::size_t VpTreeSearch::search(const TargetAnalysis& target, const Brain& brain,
                                 const SearchParams& params, const std::size_t current_block_index,
                                 std::vector<double>& block_usages) const {
    const auto* idx = brain.index();
    if (idx == nullptr) {
        throw std::runtime_error(
            "VpTreeSearch: brain.index() is null — call brain->buildIndex() before playback.");
    }

    const auto& blocks = brain.blocks();

    // O(log N) candidate retrieval using MFCC as the primary key.
    const auto candidates = idx->kNearest(target.print.mfcc, num_candidates_);
    if (candidates.empty()) {
        return 0;
    }

    // Score candidates with the full blended multi-feature distance.
    const auto [best_idx, best_score] =
        SearchUtils::scoreCandidates(candidates, target, blocks, block_usages, params);

    SearchUtils::applyUsage(block_usages, best_idx, params.usage_falloff);
    return SearchUtils::stickify(target, blocks, block_usages, best_idx, best_score,
                                 current_block_index, params);
}

}  // namespace audio::adapter::search
