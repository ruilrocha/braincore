#include "MomentumSearch.h"

#include "SearchUtils.h"

#include <algorithm>
#include <limits>

namespace audio::adapter::search {

std::size_t MomentumSearch::search(const TargetAnalysis& target, const audio::Brain& brain,
                                   const SearchParams& params,
                                   const std::size_t current_block_index,
                                   std::vector<double>& block_usages) const {
    const auto& blocks = brain.blocks();

    if (blocks.empty()) {
        return 0;
    }

    const auto& current_mfcc = blocks[current_block_index].print.mfcc;
    const double mom = std::clamp(params.momentum, 0.0, 1.0);
    const double decay = std::clamp(params.momentum_decay, 0.0, 1.0);

    // Update velocity from previous MFCC step.
    if (prev_fp_.size() == current_mfcc.size()) {
        if (velocity_.size() != current_mfcc.size()) {
            velocity_.assign(current_mfcc.size(), 0.0);
        }
        for (std::size_t i = 0; i < current_mfcc.size(); ++i) {
            const double delta = current_mfcc[i] - prev_fp_[i];
            velocity_[i] = (velocity_[i] * decay) + (delta * (1.0 - decay));
        }
    } else {
        velocity_.assign(current_mfcc.size(), 0.0);
    }
    prev_fp_ = current_mfcc;

    // Build a blended MFCC search target: actual target + predicted position.
    const auto& target_mfcc = target.print.mfcc;
    std::vector<double> blended_mfcc(target_mfcc.size());
    for (std::size_t i = 0; i < target_mfcc.size(); ++i) {
        const double predicted =
            (i < current_mfcc.size())
                ? current_mfcc[i] + ((i < velocity_.size()) ? velocity_[i] : 0.0)
                : target_mfcc[i];
        blended_mfcc[i] = (target_mfcc[i] * (1.0 - mom)) + (predicted * mom);
    }

    // Wrap blended MFCC into a TargetAnalysis (momentum only operates in MFCC space).
    TargetAnalysis blended_target;
    blended_target.print.mfcc = blended_mfcc;
    blended_target.normalised_print.mfcc = blended_mfcc;

    // Find the best-scoring block for the blended target.
    double best_score = std::numeric_limits<double>::max();
    std::size_t best_idx = 0;

    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const double usage = (i < block_usages.size()) ? block_usages[i] : 0.0;
        const double score = SearchUtils::fullScore(blended_target, blocks[i].print,
                                                    blocks[i].normalised_print, usage, params);
        if (score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    SearchUtils::applyUsage(block_usages, best_idx, params.usage_falloff);
    return SearchUtils::stickify(blended_target, blocks, block_usages, best_idx, best_score,
                                 current_block_index, params);
}

}  // namespace audio::adapter::search
