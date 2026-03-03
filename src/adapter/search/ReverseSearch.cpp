#include "ReverseSearch.h"

#include "SearchUtils.h"
#include "../../domain/port/IAnalyser.h"

namespace audio::adapter::search {

std::size_t ReverseSearch::search(
    const std::vector<double>& target_fp,
    std::vector<Block>& blocks,
    const port::IAnalyser& analyser,
    const SearchParams& params,
    const std::size_t /*current_block_index*/) const {

    double worst_score = 0.0;
    std::size_t worst_idx = 0;

    for (std::size_t i = 0; i < blocks.size(); ++i) {
        double score = analyser.distance(target_fp, blocks[i].fingerprint);
        score -= blocks[i].usage * params.usage_weight;
        if (score > worst_score) {
            worst_score = score;
            worst_idx   = i;
        }
    }

    SearchUtils::applyUsage(blocks, worst_idx, params.usage_falloff);

    return worst_idx;
}

} // namespace audio::adapter::search

