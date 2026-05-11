#include "SynapseGraph.h"

#include "Brain.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace audio {

SynapseGraph buildSynapseGraph(const Brain& brain, const std::size_t num_synapses) {
    const auto& blocks = brain.blocks();
    const auto& analyser = brain.analyser();
    const std::size_t num_blocks = blocks.size();
    const std::size_t k_synapses = std::min(num_synapses, num_blocks > 0 ? num_blocks - 1 : 0);

    SynapseGraph graph;
    graph.neighbours.resize(num_blocks);

    for (std::size_t i = 0; i < num_blocks; ++i) {
        std::vector<std::pair<std::size_t, double>> scored;
        scored.reserve(num_blocks - 1);

        for (std::size_t j = 0; j < num_blocks; ++j) {
            if (j == i) {
                continue;
            }
            const double dist = analyser.distance(blocks[i].print.mfcc, blocks[j].print.mfcc);
            scored.emplace_back(j, dist);
        }

        std::ranges::partial_sort(scored, scored.begin() + static_cast<std::ptrdiff_t>(k_synapses),
                                  [](const auto& a, const auto& b) { return a.second < b.second; });

        graph.neighbours[i].resize(k_synapses);
        for (std::size_t synapse_idx = 0; synapse_idx < k_synapses; ++synapse_idx) {
            graph.neighbours[i][synapse_idx] = scored[synapse_idx].first;
        }
    }

    return graph;
}

}  // namespace audio
