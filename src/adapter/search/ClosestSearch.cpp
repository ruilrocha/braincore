#include "ClosestSearch.h"

#include "SearchUtils.h"

#include <limits>

namespace audio::adapter::search {

std::size_t ClosestSearch::search(const SearchContext& ctx) const {
    const auto& blocks = ctx.brain.blocks();

    // Precompute normalized weights once — avoids 4 divisions per block in the inner loop.
    const auto norm_w = SearchUtils::detail::NormWeights::from(ctx.params);

    double best_score = std::numeric_limits<double>::max();
    std::size_t best_idx = 0;

    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const double usage = (i < ctx.block_usages.size()) ? ctx.block_usages[i] : 0.0;
        const double dist = SearchUtils::detail::weightedDistanceW(
            ctx.target.print, blocks[i].analysis.print, norm_w, ctx.params);
        const double n_dist = (ctx.params.n_ratio > 0.0)
                                  ? SearchUtils::detail::weightedDistanceW(
                                        ctx.target.normalised_print,
                                        blocks[i].analysis.normalised_print, norm_w, ctx.params)
                                  : 0.0;
        double score = SearchUtils::detail::blend(dist, n_dist, ctx.params.n_ratio) +
                       (usage * ctx.params.usage_weight);

        // Brightness bias (same exponential penalty as fullScore).
        if (ctx.params.brightness_weight > 0.0) {
            const auto& mel = blocks[i].analysis.normalised_print.mel;
            const std::size_t mel_n = mel.size();
            if (mel_n > 1) {
                double energy = 0.0;
                double weighted = 0.0;
                for (std::size_t k = 0; k < mel_n; ++k) {
                    const double v = mel[k];
                    energy += v;
                    weighted += static_cast<double>(k) * v;
                }
                const double brightness =
                    (energy > 1e-12) ? (weighted / energy) / static_cast<double>(mel_n - 1) : 0.5;
                const double d = brightness - ctx.params.brightness_target;
                const double bw = std::clamp(ctx.params.brightness_weight, 0.0, 1.0);
                static constexpr double kGain = 10.0;
                score *= std::exp(bw * d * d * kGain);
            }
        }

        if (score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    SearchUtils::applyUsage(ctx.block_usages, best_idx, ctx.params.usage_falloff);
    return SearchUtils::stickify(ctx.target, blocks, ctx.block_usages, best_idx, best_score,
                                 ctx.current_block_index, ctx.params);
}

}  // namespace audio::adapter::search
