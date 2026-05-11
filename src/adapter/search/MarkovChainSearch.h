#pragma once

#include "SynapseAwareSearch.h"

#include <cstddef>
#include <memory>

namespace audio::adapter::search {

/**
 * Markov-chain search: builds transition probabilities from fingerprint
 * similarity, then performs a probabilistic walk through the brain's
 * timbral space.
 *
 * The SynapseGraph is injected at construction time and must be non-null —
 * construction throws `std::invalid_argument` otherwise.
 *
 * The `temperature` parameter controls the entropy of the walk:
 *   - Low  (0.1): nearly deterministic, follows closest synapses.
 *   - High (5.0): more adventurous, explores distant timbral regions.
 */
class MarkovChainSearch final : public SynapseAwareSearch {
public:
    explicit MarkovChainSearch(double temperature, std::size_t num_synapses,
                               std::shared_ptr<const SynapseGraph> graph);

    [[nodiscard]] std::size_t search(const std::vector<double>& target_fp,
                                     const std::vector<Block>& blocks,
                                     const port::IAnalyser& analyser, const SearchParams& params,
                                     std::size_t current_block_index,
                                     std::vector<double>& block_usages) const override;

private:
    double temperature_;
    std::size_t num_synapses_;
};

}  // namespace audio::adapter::search
