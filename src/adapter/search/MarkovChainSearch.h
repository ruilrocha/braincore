#pragma once

#include "../../domain/Brain.h"
#include "../../domain/port/ISearchStrategy.h"

#include <cstddef>

namespace audio::adapter::search {

/**
 * Markov-chain search: builds transition probabilities from fingerprint
 * similarity, then performs a probabilistic walk through the brain's
 * timbral space.
 *
 * Uses the precomputed nearest-neighbour graph (`brain.index()`) to restrict
 * the candidate set to the current block's neighbours.  Requires
 * `brain->buildIndex()` to have been called before playback.  Throws
 * `std::runtime_error` if the index is absent.
 *
 * The `temperature` parameter controls the entropy of the walk:
 *   - Low  (0.1): nearly deterministic, follows closest synapses.
 *   - High (5.0): more adventurous, explores distant timbral regions.
 */
class MarkovChainSearch final : public port::ISearchStrategy {
public:
    explicit MarkovChainSearch(double temperature = 1.0, std::size_t num_synapses = 100);

    [[nodiscard]] std::size_t search(const std::vector<double>& target_fp,
                                     const audio::Brain& brain, const SearchParams& params,
                                     std::size_t current_block_index,
                                     std::vector<double>& block_usages) const override;

private:
    double temperature_;
    std::size_t num_synapses_;
};

}  // namespace audio::adapter::search
