#include "MarkovChainSearch.h"

#include "../../domain/Random.h"
#include "SearchUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace audio::adapter::search {

MarkovChainSearch::MarkovChainSearch(const double temperature, const std::size_t num_synapses)
    : temperature_(temperature), num_synapses_(num_synapses) {}

std::size_t MarkovChainSearch::search(const SearchContext& ctx) const {
    const auto& blocks = ctx.brain.blocks();

    if (blocks.empty()) {
        return 0;
    }

    if (!ctx.brain.hasIndex()) {
        throw std::runtime_error(
            "MarkovChainSearch: brain index not built — call brain.buildIndex() before playback.");
    }

    const auto neighbours = ctx.brain.neighbors(ctx.current_block_index);
    if (neighbours.empty()) {
        return rng::randomIndex(blocks.size());
    }

    const std::size_t limit = std::min(num_synapses_, neighbours.size());

    // Score each synapse: full weighted distance + usage penalty + proximity bias.
    std::vector<double> scores(limit);
    double min_score = std::numeric_limits<double>::max();

    for (std::size_t i = 0; i < limit; ++i) {
        const std::size_t block_idx = neighbours[i];
        if (block_idx >= blocks.size()) {
            continue;
        }
        const double usage =
            (block_idx < ctx.block_usages.size()) ? ctx.block_usages[block_idx] : 0.0;
        const double dist =
            SearchUtils::weightedDist(ctx.target, blocks[block_idx].analysis, ctx.params);
        const double proximity_bias = static_cast<double>(i) * 0.01;
        scores[i] = dist + (usage * ctx.params.usage_weight) + proximity_bias;
        min_score = std::min(scores[i], min_score);
    }

    // Softmax over scores (shifted for numerical stability).
    const double temp = std::max(temperature_, 1e-10);
    std::vector<double> probs(limit);
    for (std::size_t i = 0; i < limit; ++i) {
        probs[i] = std::exp(-(scores[i] - min_score) / temp);
    }
    const double total = std::accumulate(probs.begin(), probs.end(), 0.0);
    if (total <= 0.0) {
        SearchUtils::applyUsage(ctx.block_usages, ctx.current_block_index,
                                ctx.params.usage_falloff);
        return ctx.current_block_index;
    }
    for (auto& prob : probs) {
        prob /= total;
    }

    // Sample from the distribution.
    const double rand = rng::randomDouble();
    double cumulative = 0.0;
    std::size_t selected_synapse = limit - 1;
    for (std::size_t i = 0; i < limit; ++i) {
        cumulative += probs[i];
        if (rand <= cumulative) {
            selected_synapse = i;
            break;
        }
    }

    const std::size_t selected = neighbours[selected_synapse];
    SearchUtils::applyUsage(ctx.block_usages, selected, ctx.params.usage_falloff);
    return SearchUtils::stickify(ctx.target, blocks, ctx.block_usages, selected,
                                 scores[selected_synapse], ctx.current_block_index, ctx.params);
}

}  // namespace audio::adapter::search
