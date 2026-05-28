#pragma once

#include "../../domain/AudioPrint.h"
#include "../../domain/Block.h"
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
 * Centralises stickyness logic, usage handling, and multi-fingerprint
 * blended distance computation.
 *
 * All functions treat `blocks` as read-only; usage state is maintained
 * in a separate `block_usages` vector owned by the caller.
 */
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
inline double ssd(const std::vector<double>& a, const std::vector<double>& b, std::size_t start,
                  std::size_t end) {
    const std::size_t lim = std::min({end, a.size(), b.size()});
    if (lim <= start) {
        return 0.0;
    }
    double acc = 0.0;
    for (std::size_t i = start; i < lim; ++i) {
        const double d = a[i] - b[i];
        acc += d * d;
    }
    return acc / static_cast<double>(lim - start);
}

/// Normalised SSD over the full (overlapping) length of two vectors.
inline double ssdFull(const std::vector<double>& a, const std::vector<double>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n == 0)
        return 0.0;
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
    if (w_mfcc > 0.0)
        score += (w_mfcc / total_w) * ssdFull(a.mfcc, b.mfcc);
    if (w_mel > 0.0)
        score += (w_mel / total_w) * ssdFull(a.mel, b.mel);
    if (w_spectral > 0.0) {
        const auto s0 = static_cast<std::size_t>(std::max(0, params.spectral_start));
        const auto s1 = static_cast<std::size_t>(std::max(0, params.spectral_end));
        score += (w_spectral / total_w) * ssd(a.spectral, b.spectral, s0, s1);
    }
    if (w_chroma > 0.0)
        score += (w_chroma / total_w) * ssdFull(a.chroma, b.chroma);

    return score;
}

}  // namespace detail

// ── Core scoring functions ─────────────────────────────────────────────

/**
 * Weighted distance between a TargetAnalysis and a candidate block's prints.
 *
 * Applies n_ratio to blend between raw and amplitude-normalised comparisons.
 * Does NOT include the usage penalty — add that separately if needed.
 */
inline double weightedDist(const TargetAnalysis& target, const AudioPrint& candidate_print,
                           const AudioPrint& candidate_norm_print, const SearchParams& params) {
    double d = detail::weightedDistance(target.print, candidate_print, params);
    if (params.n_ratio > 0.0) {
        const double nd =
            detail::weightedDistance(target.normalised_print, candidate_norm_print, params);
        d = detail::blend(d, nd, params.n_ratio);
    }
    return d;
}

/**
 * Full score for a candidate block: weighted distance + usage penalty.
 *
 * @param target          Raw + normalised target fingerprints.
 * @param candidate_print Raw AudioPrint of the candidate block.
 * @param candidate_norm  Normalised AudioPrint of the candidate block.
 * @param candidate_usage Current usage counter for the candidate.
 * @param params          Search parameters.
 * @return                Score (lower = better match).
 */
inline double fullScore(const TargetAnalysis& target, const AudioPrint& candidate_print,
                        const AudioPrint& candidate_norm, double candidate_usage,
                        const SearchParams& params) {
    return weightedDist(target, candidate_print, candidate_norm, params) +
           (candidate_usage * params.usage_weight);
}

// ── Stickyness ─────────────────────────────────────────────────────────

/**
 * Apply stickyness: if the next sequential block scores well enough compared
 * to the global winner, prefer it for temporal coherence.
 */
inline std::size_t stickify(const TargetAnalysis& target, const std::vector<Block>& blocks,
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
    const double next_score =
        fullScore(target, blocks[next].print, blocks[next].normalised_print, next_usage, params);

    if (next_score * (1.0 - params.stickyness) < closest_score * params.stickyness) {
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
inline std::pair<std::size_t, double> scoreCandidates(const IndexRange& indices,
                                                      const TargetAnalysis& target,
                                                      const std::vector<Block>& blocks,
                                                      const std::vector<double>& block_usages,
                                                      const SearchParams& params) {
    double best_score = std::numeric_limits<double>::max();
    std::size_t best_idx = 0;

    for (const std::size_t i : indices) {
        if (i >= blocks.size())
            continue;
        const double usage = (i < block_usages.size()) ? block_usages[i] : 0.0;
        const double score =
            fullScore(target, blocks[i].print, blocks[i].normalised_print, usage, params);
        if (score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }
    return {best_idx, best_score};
}

}  // namespace audio::adapter::search::SearchUtils
