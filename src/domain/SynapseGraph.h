#pragma once

#include "constants.h"

#include <cstddef>
#include <vector>

namespace audio {

class Brain;

/**
 * A pre-computed nearest-neighbour graph over the blocks of a Brain.
 *
 * For each block i, `neighbours[i]` is a list of block indices sorted by
 * ascending fingerprint distance — the most similar blocks first.
 *
 * SynapseGraph is deliberately separate from Brain and Block so that:
 *   - Brain remains a pure immutable data container (no hidden build cost).
 *   - Only the search strategies that need the graph receive it (constructor
 *     injection); strategies that do not need it are unaffected.
 *   - `block.synapses` dead weight on every block is eliminated.
 *
 * Build with the free function `buildSynapseGraph()` *before* playback starts
 * and inject the result into the strategy constructors that require it
 * (SynapticSearch, MarkovChainSearch).
 */
struct SynapseGraph {
    /// `neighbours[i]` — sorted ascending by fingerprint distance.
    std::vector<std::vector<std::size_t>> neighbours;

    [[nodiscard]] bool empty() const { return neighbours.empty(); }
    [[nodiscard]] std::size_t size() const { return neighbours.size(); }
};

/**
 * Build a SynapseGraph from a Brain's fingerprints.
 *
 * For every block, computes distances to all other blocks and retains the
 * @p num_synapses closest ones, sorted ascending.
 *
 * Complexity: O(N² × fingerprint_size).  Call this once, before playback.
 *
 * @param brain        Source of blocks and analyser.
 * @param num_synapses Number of nearest neighbours to keep per block.
 */
SynapseGraph buildSynapseGraph(const Brain& brain, std::size_t num_synapses = kDefaultNumSynapses);

}  // namespace audio
