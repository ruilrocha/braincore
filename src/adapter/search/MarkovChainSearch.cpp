#include "MarkovChainSearch.h"

#include "../../domain/Random.h"
#include "../../domain/port/IAnalyser.h"
#include "SearchUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace audio::adapter::search {

MarkovChainSearch::MarkovChainSearch(const double temperature, const std::size_t num_synapses)
    : temperature_(temperature), num_synapses_(num_synapses) {}

std::size_t MarkovChainSearch::search(const std::vector<double>& target_fp,
                                      std::vector<Block>& blocks, const port::IAnalyser& analyser,
                                      const SearchParams& params,
                                      const std::size_t current_block_index) const {
    if (blocks.empty()) {
        return 0;
    }

    const auto& current = blocks[current_block_index];

    // Fall back to the closest search if no synapses are available.
    if (current.synapses.empty()) {
        double best = std::numeric_limits<double>::max();
        std::size_t best_idx = 0;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            const double d = analyser.distance(target_fp, blocks[i].print.mfcc) +
                             (blocks[i].usage * params.usage_weight);
            if (d < best) {
                best = d;
                best_idx = i;
            }
        }
        SearchUtils::applyUsage(blocks, best_idx, params.usage_falloff);
        return best_idx;
    }

    const std::size_t limit = std::min(num_synapses_, current.synapses.size());

    // 1. Score each synapse candidate by combined distance: how well it
    //    matches the target AND how close it is to the current position.
    //    This produces transitions that are both target-relevant and smooth.
    std::vector<double> scores(limit);
    double min_score = std::numeric_limits<double>::max();

    for (std::size_t s = 0; s < limit; ++s) {
        const std::size_t idx = current.synapses[s];
        // Target affinity: how well does this candidate match the target?
        const double target_dist = analyser.distance(target_fp, blocks[idx].print.mfcc);
        // Synapse proximity: how close is it to the current block? (implicit
        // from ordering — earlier synapses are closer).
        const double proximity_bonus = static_cast<double>(s) * 0.01;
        scores[s] = target_dist + proximity_bonus + (blocks[idx].usage * params.usage_weight);
        min_score = std::min(scores[s], min_score);
    }

    // 2. Convert to softmax probabilities.
    const double T = std::max(temperature_, 1e-10);
    std::vector<double> probs(limit);
    for (std::size_t s = 0; s < limit; ++s) {
        probs[s] = std::exp(-(scores[s] - min_score) / T);
    }

    const double total = std::accumulate(probs.begin(), probs.end(), 0.0);
    if (total <= 0.0) {
        SearchUtils::applyUsage(blocks, current_block_index, params.usage_falloff);
        return current_block_index;
    }
    for (auto& p : probs) {
        p /= total;
    }

    // 3. Sample from the distribution.
    const double r = rng::randomDouble();
    double cumulative = 0.0;
    std::size_t selected_synapse = limit - 1;
    for (std::size_t s = 0; s < limit; ++s) {
        cumulative += probs[s];
        if (r <= cumulative) {
            selected_synapse = s;
            break;
        }
    }

    const std::size_t selected = current.synapses[selected_synapse];
    SearchUtils::applyUsage(blocks, selected, params.usage_falloff);

    return SearchUtils::stickify(target_fp, blocks, analyser, selected, scores[selected_synapse],
                                 current_block_index, params.stickyness);
}

}  // namespace audio::adapter::search
