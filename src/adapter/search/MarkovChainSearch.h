#pragma once

#include "../../domain/port/ISearchStrategy.h"

#include <cstddef>

namespace audio::adapter::search {

/**
 * Markov-chain search: builds transition probabilities from fingerprint
 * similarity, then performs a probabilistic walk through the brain's
 * timbral space.
 *
 * Unlike SynapticSearch (which picks the closest synapse), MarkovChainSearch
 * samples from a softmax distribution over the synapse list, producing
 * output that "flows" through the brain with musical coherence but
 * controlled randomness.
 *
 * Requires Brain::buildSynapses() to have been called first.
 *
 * The `temperature` parameter controls the entropy of the walk:
 *   - Low  (0.1): nearly deterministic, follows closest synapses.
 *   - High (5.0): more adventurous, explores distant timbral regions.
 *
 * Works well with stickyness and momentum for even smoother transitions.
 */
class MarkovChainSearch final : public port::ISearchStrategy {
public:
    /**
     * @param temperature  Softmax temperature for transition probabilities.
     * @param num_synapses Maximum number of synapses to consider per step.
     */
    explicit MarkovChainSearch(double temperature = 1.0, std::size_t num_synapses = 100);

    [[nodiscard]] std::size_t search(const std::vector<double>& target_fp,
                                     std::vector<Block>& blocks, const port::IAnalyser& analyser,
                                     const SearchParams& params,
                                     std::size_t current_block_index) const override;

    [[nodiscard]] bool requiresSynapses() const override { return true; }

private:
    double temperature_;
    std::size_t num_synapses_;
};

}  // namespace audio::adapter::search
