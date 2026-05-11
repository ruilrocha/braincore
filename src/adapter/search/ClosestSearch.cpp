#include "ClosestSearch.h"

#include "../../domain/port/IAnalyser.h"
#include "SearchUtils.h"

#include <limits>

namespace audio::adapter::search {

std::size_t ClosestSearch::search(const std::vector<double>& target_fp, std::vector<Block>& blocks,
                                  const port::IAnalyser& analyser, const SearchParams& params,
                                  const std::size_t current_block_index) const {
    double best_score = std::numeric_limits<double>::max();
    std::size_t best_idx = 0;

    for (std::size_t i = 0; i < blocks.size(); ++i) {
        double score = analyser.distance(target_fp, blocks[i].mfcc);
        score += blocks[i].usage * params.usage_weight;
        if (score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    SearchUtils::applyUsage(blocks, best_idx, params.usage_falloff);

    return SearchUtils::stickify(target_fp, blocks, analyser, best_idx, best_score,
                                 current_block_index, params.stickyness);
}

}  // namespace audio::adapter::search
