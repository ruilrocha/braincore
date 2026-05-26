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

std::size_t MarkovChainSearch::search(const std::vector<double>& target_fp,
                                      const audio::Brain& brain, const SearchParams& params,
                                      const std::size_t current_block_index,
                                      std::vector<double>& block_usages) const {
    const auto& blocks = brain.blocks();
    const auto& analyser = brain.analyser();

    if (blocks.empty()) {
        return 0;
    }

    const auto* idx = brain.index();
    if (idx == nullptr) {
        throw std::runtime_error(
            "MarkovChainSearch: brain.index() is null — call brain->buildIndex() before playback.");
    }

    const auto neighbours = idx->neighbors(current_block_index);
    if (neighbours.empty()) {
        return rng::randomIndex(blocks.size());
    }

    const std::size_t limit = std::min(num_synapses_, neighbours.size());

    // 1. Score each synapse candidate: target affinity + proximity bonus + usage penalty.
    std::vector<double> scores(limit);
    double min_score = std::numeric_limits<double>::max();

    for (std::size_t candidate_idx = 0; candidate_idx < limit; ++candidate_idx) {
        const std::size_t block_idx = neighbours[candidate_idx];
        const double usage = (block_idx < block_usages.size()) ? block_usages[block_idx] : 0.0;
        const double target_dist = analyser.distance(target_fp, blocks[block_idx].print.mfcc);
        const double proximity_bonus = static_cast<double>(candidate_idx) * 0.01;
        scores[candidate_idx] = target_dist + proximity_bonus + (usage * params.usage_weight);
        min_score = std::min(scores[candidate_idx], min_score);
    }

    // 2. Convert to softmax probabilities.
    const double temp = std::max(temperature_, 1e-10);
    std::vector<double> probs(limit);
    for (std::size_t prob_idx = 0; prob_idx < limit; ++prob_idx) {
        probs[prob_idx] = std::exp(-(scores[prob_idx] - min_score) / temp);
    }

    const double total = std::accumulate(probs.begin(), probs.end(), 0.0);
    if (total <= 0.0) {
        SearchUtils::applyUsage(block_usages, current_block_index, params.usage_falloff);
        return current_block_index;
    }
    for (auto& prob : probs) {
        prob /= total;
    }

    // 3. Sample from the distribution.
    const double random_double = rng::randomDouble();
    double cumulative = 0.0;
    std::size_t selected_synapse = limit - 1;
    for (std::size_t synapse_idx = 0; synapse_idx < limit; ++synapse_idx) {
        cumulative += probs[synapse_idx];
        if (random_double <= cumulative) {
            selected_synapse = synapse_idx;
            break;
        }
    }

    const std::size_t selected = neighbours[selected_synapse];
    SearchUtils::applyUsage(block_usages, selected, params.usage_falloff);

    return SearchUtils::stickify(target_fp, blocks, analyser, selected, scores[selected_synapse],
                                 current_block_index, params.stickyness);
}

}  // namespace audio::adapter::search
