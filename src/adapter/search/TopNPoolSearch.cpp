#include "TopNPoolSearch.h"

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

#include "SearchUtils.h"
#include "../../domain/port/IAnalyser.h"

namespace audio::adapter::search {

std::size_t TopNPoolSearch::search(
    const std::vector<double>& target_fp,
    std::vector<Block>& blocks,
    const port::IAnalyser& analyser,
    const SearchParams& params,
    const std::size_t current_block_index) const {

    if (blocks.empty()) return 0;

    const auto n = blocks.size();
    const auto pool = static_cast<std::size_t>(
        std::clamp(params.pool_size, 1, static_cast<int>(n)));

    // 1. Score all blocks.
    std::vector<std::pair<double, std::size_t>> scored(n);
    for (std::size_t i = 0; i < n; ++i) {
        double score = analyser.distance(target_fp, blocks[i].fingerprint);
        score += blocks[i].usage * params.usage_weight;
        scored[i] = {score, i};
    }

    // 2. Partial sort to get only the top-N closest.
    std::partial_sort(scored.begin(),
                      scored.begin() + static_cast<std::ptrdiff_t>(pool),
                      scored.end(),
                      [](const auto& a, const auto& b) {
                          return a.first < b.first;
                      });

    // 3. Pick uniformly at random from the pool.
    const std::size_t pick = static_cast<std::size_t>(std::rand()) % pool;
    const std::size_t selected = scored[pick].second;
    const double selected_dist = scored[pick].first;

    SearchUtils::applyUsage(blocks, selected, params.usage_falloff);

    return SearchUtils::stickify(target_fp, blocks, analyser,
                                 selected, selected_dist,
                                 current_block_index, params.stickyness);
}

} // namespace audio::adapter::search

