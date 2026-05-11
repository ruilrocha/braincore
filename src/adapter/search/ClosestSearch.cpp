#include "ClosestSearch.h"

#include "../../domain/port/IAnalyser.h"
#include "SearchUtils.h"

#include <numeric>

namespace audio::adapter::search {

std::size_t ClosestSearch::search(const std::vector<double>& target_fp,
                                  const std::vector<Block>& blocks, const port::IAnalyser& analyser,
                                  const SearchParams& params, const std::size_t current_block_index,
                                  std::vector<double>& block_usages) const {
    // Build a range over all block indices and score them.
    std::vector<std::size_t> all_indices(blocks.size());
    std::iota(all_indices.begin(), all_indices.end(), 0);

    const auto [best_idx, best_score] = SearchUtils::scoreCandidates(
        all_indices, target_fp, blocks, analyser, block_usages, params);

    SearchUtils::applyUsage(block_usages, best_idx, params.usage_falloff);

    return SearchUtils::stickify(target_fp, blocks, analyser, best_idx, best_score,
                                 current_block_index, params.stickyness);
}

}  // namespace audio::adapter::search
