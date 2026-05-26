#include "MomentumSearch.h"

#include "SearchUtils.h"

#include <limits>

namespace audio::adapter::search {

std::size_t MomentumSearch::search(const std::vector<double>& target_fp, const audio::Brain& brain,
                                   const SearchParams& params,
                                   const std::size_t current_block_index,
                                   std::vector<double>& block_usages) const {
    const auto& blocks = brain.blocks();
    const auto& analyser = brain.analyser();

    if (blocks.empty()) {
        return 0;
    }

    const auto& current_fp = blocks[current_block_index].print.mfcc;
    const double mom = std::clamp(params.momentum, 0.0, 1.0);
    const double decay = std::clamp(params.momentum_decay, 0.0, 1.0);

    // ── Update velocity from previous step ─────────────────────────────
    if (prev_fp_.size() == current_fp.size()) {
        if (velocity_.size() != current_fp.size()) {
            velocity_.assign(current_fp.size(), 0.0);
        }
        for (std::size_t i = 0; i < current_fp.size(); ++i) {
            const double delta = current_fp[i] - prev_fp_[i];
            velocity_[i] = (velocity_[i] * decay) + (delta * (1.0 - decay));
        }
    } else {
        velocity_.assign(current_fp.size(), 0.0);
    }
    prev_fp_ = current_fp;

    // ── Build the search target: blend actual target with predicted pos ─
    std::vector<double> search_target(target_fp.size());
    for (std::size_t i = 0; i < target_fp.size(); ++i) {
        const double predicted = (i < current_fp.size())
                                     ? current_fp[i] + ((i < velocity_.size()) ? velocity_[i] : 0.0)
                                     : target_fp[i];
        search_target[i] = (target_fp[i] * (1.0 - mom)) + (predicted * mom);
    }

    // ── Find the closest block to the blended search target ────────────
    double best_score = std::numeric_limits<double>::max();
    std::size_t best_idx = 0;

    for (std::size_t i = 0; i < blocks.size(); ++i) {
        double score = analyser.distance(search_target, blocks[i].print.mfcc);
        const double usage = (i < block_usages.size()) ? block_usages[i] : 0.0;
        score += usage * params.usage_weight;
        if (score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    SearchUtils::applyUsage(block_usages, best_idx, params.usage_falloff);

    return SearchUtils::stickify(search_target, blocks, analyser, best_idx, best_score,
                                 current_block_index, params.stickyness);
}

}  // namespace audio::adapter::search
