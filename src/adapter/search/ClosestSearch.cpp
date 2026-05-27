#include "ClosestSearch.h"

#include "SearchUtils.h"

#include <limits>

namespace audio::adapter::search {

std::size_t ClosestSearch::search(const TargetAnalysis& target, const audio::Brain& brain,
                                  const SearchParams& params, const std::size_t current_block_index,
                                  std::vector<double>& block_usages) const {
    const auto& blocks = brain.blocks();

    double best_score = std::numeric_limits<double>::max();
    std::size_t best_idx = 0;

    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const double usage = (i < block_usages.size()) ? block_usages[i] : 0.0;
        const double score = SearchUtils::fullScore(target, blocks[i].print,
                                                    blocks[i].normalised_print, usage, params);
        if (score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    SearchUtils::applyUsage(block_usages, best_idx, params.usage_falloff);
    return SearchUtils::stickify(target, blocks, block_usages, best_idx, best_score,
                                 current_block_index, params);
}

}  // namespace audio::adapter::search
