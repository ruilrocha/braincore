#include "SynapticSearch.h"

#include "../../domain/port/IAnalyser.h"
#include "SearchUtils.h"

#include <algorithm>
#include <limits>

namespace audio::adapter::search {

SynapticSearch::SynapticSearch(const std::size_t num_synapses) : num_synapses_(num_synapses) {}

std::size_t SynapticSearch::search(const std::vector<double>& target_fp, std::vector<Block>& blocks,
                                   const port::IAnalyser& analyser, const SearchParams& params,
                                   const std::size_t current_block_index) const {
    const auto& current = blocks[current_block_index];

    if (current.synapses.empty()) {
        return current_block_index;
    }

    const std::size_t limit = std::min(num_synapses_, current.synapses.size());

    double best_score = std::numeric_limits<double>::max();
    std::size_t best_idx = current_block_index;

    for (std::size_t s = 0; s < limit; ++s) {
        const std::size_t idx = current.synapses[s];
        double score = analyser.distance(target_fp, blocks[idx].mfcc);
        score += blocks[idx].usage * params.usage_weight;
        if (score < best_score) {
            best_score = score;
            best_idx = idx;
        }
    }

    SearchUtils::applyUsage(blocks, best_idx, params.usage_falloff);

    return best_idx;
}

}  // namespace audio::adapter::search
