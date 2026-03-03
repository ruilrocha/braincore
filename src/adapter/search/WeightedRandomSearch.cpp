#include "WeightedRandomSearch.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "SearchUtils.h"
#include "../../domain/Random.h"
#include "../../domain/port/IAnalyser.h"

namespace audio::adapter::search {

WeightedRandomSearch::WeightedRandomSearch(const double temperature)
    : temperature_(temperature) {}

std::size_t WeightedRandomSearch::search(
    const std::vector<double>& target_fp,
    std::vector<Block>& blocks,
    const port::IAnalyser& analyser,
    const SearchParams& params,
    const std::size_t current_block_index) const {

    if (blocks.empty()) return 0;

    const auto n = blocks.size();

    // 1. Compute distances for all blocks.
    std::vector<double> distances(n);
    for (std::size_t i = 0; i < n; ++i) {
        distances[i] = analyser.distance(target_fp, blocks[i].fingerprint)
                     + blocks[i].usage * params.usage_weight;
    }

    // 2. Convert to weights: w_i = exp(-dist_i / temperature).
    //    Softmax-style: shift by min to avoid numerical issues.
    const double min_d = *std::ranges::min_element(distances);
    std::vector<double> weights(n);
    for (std::size_t i = 0; i < n; ++i) {
        weights[i] = std::exp(-(distances[i] - min_d) / std::max(temperature_, 1e-10));
    }

    // 3. Build cumulative distribution.
    const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
    if (total <= 0.0) {
        // Fallback: uniform random.
        const std::size_t idx = rng::randomIndex(n);
        SearchUtils::applyUsage(blocks, idx, params.usage_falloff);
        return idx;
    }

    // Normalise to probabilities.
    for (auto& w : weights) w /= total;

    // 4. Sample from the distribution.
    const double r = rng::randomDouble();
    double cumulative = 0.0;
    std::size_t selected = n - 1;
    for (std::size_t i = 0; i < n; ++i) {
        cumulative += weights[i];
        if (r <= cumulative) {
            selected = i;
            break;
        }
    }

    SearchUtils::applyUsage(blocks, selected, params.usage_falloff);

    return SearchUtils::stickify(target_fp, blocks, analyser,
                                 selected, distances[selected],
                                 current_block_index, params.stickyness);
}

} // namespace audio::adapter::search

