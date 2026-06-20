#pragma once

#include "../../domain/Block.h"
#include "../../domain/BlockAnalysis.h"
#include "../../domain/SearchParams.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

/**
 * Shared utilities for search strategy adapters.
 *
 * Centralises stickyness logic, usage handling, multi-fingerprint
 * blended distance computation, and momentum state.
 *
 * All functions treat `blocks` as read-only; usage state is maintained
 * in a separate `block_usages` vector owned by the caller.
 */
namespace audio::adapter::search {

/**
 * Reusable momentum state for search strategies.
 *
 * Tracks a velocity vector in MFCC space so that search targets drift
 * smoothly through the brain's timbral landscape.  Declare as a
 * `mutable` member of any strategy class to add momentum support.
 *
 * Usage:
 *   auto effective = momentum_state_.blend(ctx.target,
 *                        blocks[ctx.current_block_index].analysis.print.mfcc,
 *                        ctx.params);
 *   // … search against effective_target …
 *
 * The state is always updated (even when momentum == 0), so turning on
 * momentum mid-stream produces a smooth result with no sudden jump.
 */
struct MomentumState {
    mutable std::vector<double> velocity;  ///< Running velocity in MFCC space.
    mutable std::vector<float> prev_fp;    ///< MFCC from the previous block.

    /**
     * Evolve velocity from @p current_mfcc, then return a BlockAnalysis
     * whose MFCC is blended between @p target and the predicted position.
     *
     * When momentum == 0 the original target is returned unchanged (fast path).
     * All non-MFCC fields (mel, spectral, chroma) are copied from @p target.
     */
    [[nodiscard]] BlockAnalysis blend(const BlockAnalysis& target,
                                      const std::vector<float>& current_mfcc,
                                      const SearchParams& params) const {
        const double mom = std::clamp(params.momentum, 0.0, 1.0);
        const double decay = std::clamp(params.momentum_decay, 0.0, 1.0);
        const std::size_t n = current_mfcc.size();

        // Update velocity from the delta since the last block.
        if (prev_fp.size() == n) {
            if (velocity.size() != n) {
                velocity.assign(n, 0.0);
            }
            for (std::size_t i = 0; i < n; ++i) {
                const double delta =
                    static_cast<double>(current_mfcc[i]) - static_cast<double>(prev_fp[i]);
                velocity[i] = (velocity[i] * decay) + (delta * (1.0 - decay));
            }
        } else {
            velocity.assign(n, 0.0);
        }
        prev_fp = current_mfcc;

        if (mom <= 0.0) {
            return target;  // fast path: nothing to blend
        }

        // Build blended MFCC: (1-mom)*target + mom*(current + velocity)
        const auto& target_mfcc = target.print.mfcc;
        std::vector<float> blended(target_mfcc.size());
        for (std::size_t i = 0; i < target_mfcc.size(); ++i) {
            const double predicted = (i < n) ? static_cast<double>(current_mfcc[i]) +
                                                   (i < velocity.size() ? velocity[i] : 0.0)
                                             : static_cast<double>(target_mfcc[i]);
            blended[i] = static_cast<float>((static_cast<double>(target_mfcc[i]) * (1.0 - mom)) +
                                            (predicted * mom));
        }

        // Copy target, then replace only the MFCC fields.
        BlockAnalysis result = target;
        result.print.mfcc = blended;
        result.normalised_print.mfcc = blended;
        return result;
    }

    void reset() const noexcept {
        velocity.clear();
        prev_fp.clear();
    }
};

}  // namespace audio::adapter::search

namespace audio::adapter::search::SearchUtils {

// ── Usage tracking ─────────────────────────────────────────────────────

/**
 * Deplete all usage counters by `falloff` then increment the selected block's counter.
 */
inline void applyUsage(std::vector<double>& block_usages, const std::size_t selected_idx,
                       const double falloff) {
    if (falloff < 1.0) {
        for (auto& u : block_usages) {
            u *= falloff;
        }
    }
    if (selected_idx < block_usages.size()) {
        block_usages[selected_idx] += kUsageFactor;
    }
}

// ── Multi-feature distance computation ────────────────────────────────

namespace detail {

/// Sum of squared differences over a range [start, end), normalised by range length.
inline double ssd(const std::vector<float>& a, const std::vector<float>& b, std::size_t start,
                  std::size_t end) {
    const std::size_t lim = std::min({end, a.size(), b.size()});
    if (lim <= start) {
        return 0.0;
    }
    double acc = 0.0;
    for (std::size_t i = start; i < lim; ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        acc += d * d;
    }
    return acc / static_cast<double>(lim - start);
}

/// Normalised SSD over the full (overlapping) length of two vectors.
inline double ssdFull(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n == 0) {
        return 0.0;
    }
    return ssd(a, b, 0, n);
}

/// Linear blend: a*(1−t) + b*t.
inline double blend(double a, double b, double t) {
    return (a * (1.0 - t)) + (b * t);
}

/**
 * Weighted multi-feature distance between two AudioPrint bundles.
 *
 * Weights are normalised to sum = 1.0 so each slider is a true percentage.
 * Falls back to pure MFCC when all weights are zero.
 */
inline double weightedDistance(const AudioPrint& a, const AudioPrint& b,
                               const SearchParams& params) {
    const double w_mfcc = std::max(0.0, params.mfcc_weight);
    const double w_mel = std::max(0.0, params.mel_weight);
    const double w_spectral = std::max(0.0, params.spectral_weight);
    const double w_chroma = std::max(0.0, params.chroma_weight);
    const double total_w = w_mfcc + w_mel + w_spectral + w_chroma;

    if (total_w < 1e-12) {
        return ssdFull(a.mfcc, b.mfcc);  // pure MFCC fallback
    }

    double score = 0.0;
    if (w_mfcc > 0.0) {
        score += (w_mfcc / total_w) * ssdFull(a.mfcc, b.mfcc);
    }
    if (w_mel > 0.0) {
        score += (w_mel / total_w) * ssdFull(a.mel, b.mel);
    }
    if (w_spectral > 0.0) {
        const auto s0 = static_cast<std::size_t>(std::max(0, params.spectral_start));
        const auto s1 = static_cast<std::size_t>(std::max(0, params.spectral_end));
        score += (w_spectral / total_w) * ssd(a.spectral, b.spectral, s0, s1);
    }
    if (w_chroma > 0.0) {
        score += (w_chroma / total_w) * ssdFull(a.chroma, b.chroma);
    }

    return score;
}

}  // namespace detail

// ── Core scoring functions ─────────────────────────────────────────────

/**
 * Weighted distance between two BlockAnalysis bundles.
 *
 * Applies n_ratio to blend between raw and amplitude-normalised comparisons.
 * Does NOT include the usage penalty — add that separately if needed.
 */
inline double weightedDist(const BlockAnalysis& target, const BlockAnalysis& candidate,
                           const SearchParams& params) {
    double d = detail::weightedDistance(target.print, candidate.print, params);
    if (params.n_ratio > 0.0) {
        const double nd =
            detail::weightedDistance(target.normalised_print, candidate.normalised_print, params);
        d = detail::blend(d, nd, params.n_ratio);
    }
    return d;
}

/**
 * Full score for a candidate block: weighted distance + usage penalty.
 *
 * @param target          Target block analysis (raw + normalised fingerprints).
 * @param candidate       Candidate block analysis (raw + normalised fingerprints).
 * @param candidate_usage Current usage counter for the candidate.
 * @param params          Search parameters.
 * @return                Score (lower = better match).
 */
inline double fullScore(const BlockAnalysis& target, const BlockAnalysis& candidate,
                        double candidate_usage, const SearchParams& params) {
    return weightedDist(target, candidate, params) + (candidate_usage * params.usage_weight);
}

// ── Stickyness ─────────────────────────────────────────────────────────

/**
 * Apply stickyness: bias toward the next sequential block for temporal coherence.
 *
 * `stickyness` controls how much of a score advantage the next block receives.
 * At 0, it must actually be the best match; at 1 it can be up to kMaxStickyRatio
 * times worse than the global winner. The cap prevents forcing obviously bad
 * blocks (e.g. silence) regardless of stickyness value.
 */
inline std::size_t stickify(const BlockAnalysis& target, const std::vector<Block>& blocks,
                            const std::vector<double>& block_usages, const std::size_t closest_idx,
                            const double closest_score, const std::size_t current_idx,
                            const SearchParams& params) {
    if (params.stickyness <= 0.0) {
        return closest_idx;
    }
    const std::size_t next = current_idx + 1;
    if (next >= blocks.size()) {
        return closest_idx;
    }
    const double next_usage = (next < block_usages.size()) ? block_usages[next] : 0.0;

    if (const double next_score = fullScore(target, blocks[next].analysis, next_usage, params);
        next_score * (1.0 - params.stickyness) < closest_score * params.stickyness) {
        return next;
    }
    return closest_idx;
}

// ── Candidate scoring ──────────────────────────────────────────────────

/**
 * Score a range of candidate block indices and return the best-matching one.
 *
 * @param indices      Range of candidate indices into @p blocks.
 * @param target       Raw + normalised target fingerprints.
 * @param blocks       All blocks in the brain (read-only).
 * @param block_usages Per-block usage counters.
 * @param params       Search parameters.
 * @return             {best_index, best_score}.
 */
template <typename IndexRange>
std::pair<std::size_t, double> scoreCandidates(const IndexRange& indices,
                                               const BlockAnalysis& target,
                                               const std::vector<Block>& blocks,
                                               const std::vector<double>& block_usages,
                                               const SearchParams& params) {
    double best_score = std::numeric_limits<double>::max();
    std::size_t best_idx = 0;

    for (const std::size_t i : indices) {
        if (i >= blocks.size()) {
            continue;
        }
        const double usage = (i < block_usages.size()) ? block_usages[i] : 0.0;
        if (const double score = fullScore(target, blocks[i].analysis, usage, params);
            score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }
    return {best_idx, best_score};
}

}  // namespace audio::adapter::search::SearchUtils
