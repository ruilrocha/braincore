#pragma once

#include "../../domain/Block.h"
#include "../../domain/SearchParams.h"
#include "../../domain/port/IAnalyser.h"

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

/**
 * Apply stickyness: if the next sequential block is "close enough" compared
 * to the global winner, prefer it for temporal coherence.
 */
inline std::size_t stickify(const std::vector<double>& target_fp, const std::vector<Block>& blocks,
                            const port::IAnalyser& analyser, const std::size_t closest_idx,
                            const double closest_dist, const std::size_t current_idx,
                            const double stickyness) {
    if (stickyness <= 0.0) {
        return closest_idx;
    }

    const std::size_t next = current_idx + 1;
    if (next >= blocks.size()) {
        return closest_idx;
    }

    const double next_dist = analyser.distance(target_fp, blocks[next].print.mfcc);

    if (next_dist * (1.0 - stickyness) < closest_dist * stickyness) {
        return next;
    }
    return closest_idx;
}

/**
 * Deplete all usage counters and increment the selected block's counter.
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

// ── Multi-feature blended distance computation ─────────────────────────

namespace detail {

/// Sum of squared differences over a range [start, end), normalised by length.
inline double ssd(const std::vector<double>& a, const std::vector<double>& b, std::size_t start,
                  std::size_t end) {
    double acc = 0.0;
    const std::size_t lim = std::min({end, a.size(), b.size()});
    for (std::size_t i = start; i < lim; ++i) {
        const double d = a[i] - b[i];
        acc += d * d;
    }
    const std::size_t n = lim > start ? lim - start : 1;
    return acc / static_cast<double>(n);
}

/// Normalised SSD over the full vector.
inline double ssdFull(const std::vector<double>& a, const std::vector<double>& b) {
    const std::size_t n = std::max(std::min(a.size(), b.size()), std::size_t{1});
    return ssd(a, b, 0, n);
}

/**
 * Compute the full multi-feature distance between two AudioPrint bundles.
 *
 * Weights from @p params are normalised so they sum to 1.0, making each
 * weight a true percentage contribution.  If all weights are zero, pure
 * MFCC distance is used (backwards-compatible fallback).
 *
 * Active features: mfcc, mel, spectral, chroma.
 *
 * @param raw_a      Raw (or normalised) AudioPrint of block A.
 * @param raw_b      Raw (or normalised) AudioPrint of block B.
 * @param params     Search parameters with per-feature weights.
 * @return           Weighted distance score (lower = better match).
 */
inline double weightedDistance(const AudioPrint& raw_a, const AudioPrint& raw_b,
                               const SearchParams& params) {
    const double w_mfcc = std::max(0.0, params.mfcc_weight);
    const double w_mel = std::max(0.0, params.mel_weight);
    const double w_spectral = std::max(0.0, params.spectral_weight);
    const double w_chroma = std::max(0.0, params.chroma_weight);

    const double total_w = w_mfcc + w_mel + w_spectral + w_chroma;

    // Pure MFCC fallback when all weights are zero.
    if (total_w < 1e-12) {
        return ssdFull(raw_a.mfcc, raw_b.mfcc);
    }

    double score = 0.0;

    if (w_mfcc > 0.0) {
        score += (w_mfcc / total_w) * ssdFull(raw_a.mfcc, raw_b.mfcc);
    }
    if (w_mel > 0.0) {
        score += (w_mel / total_w) * ssdFull(raw_a.mel, raw_b.mel);
    }
    if (w_spectral > 0.0) {
        const auto sec_start = static_cast<std::size_t>(std::max(0, params.spectral_start));
        const auto sec_end = static_cast<std::size_t>(std::max(0, params.spectral_end));
        score += (w_spectral / total_w) * ssd(raw_a.spectral, raw_b.spectral, sec_start, sec_end);
    }
    if (w_chroma > 0.0) {
        score += (w_chroma / total_w) * ssdFull(raw_a.chroma, raw_b.chroma);
    }

    return score;
}

/// Linear blend: a*(1-t) + b*t.
inline double blend(double a, double b, double t) {
    return (a * (1.0 - t)) + (b * t);
}

}  // namespace detail

/**
 * Compute the full blended distance between a target block and a candidate
 * brain block, considering:
 *   - per-feature weights (mfcc, mel, spectral, chroma, pitch), normalised to sum = 1.0
 *   - raw vs. normalised fingerprint blend (n_ratio)
 *   - usage penalty (novelty), read from @p block_usages
 *
 * @return Combined distance score (lower = better match).
 */
inline double fullScore(const Block& target, const Block& candidate,
                        const std::vector<double>& block_usages, const std::size_t candidate_idx,
                        const SearchParams& params) {
    // Raw comparison.
    double dist = detail::weightedDistance(target.print, candidate.print, params);

    // Blend in normalised comparison (if n_ratio > 0).
    if (params.n_ratio > 0.0) {
        const double norm_dist =
            detail::weightedDistance(target.normalised_print, candidate.normalised_print, params);
        dist = detail::blend(dist, norm_dist, params.n_ratio);
    }

    // Usage penalty ("novelty").
    if (candidate_idx < block_usages.size()) {
        dist += block_usages[candidate_idx] * params.usage_weight;
    }
    return dist;
}

/**
 * Score a range of candidate block indices and return the best-matching one.
 *
 * Uses `analyser.distance(target_fp, block.print.mfcc) + usage_weight * usage`
 * — the same scoring used by all deterministic strategies.  Centralises the
 * loop so ClosestSearch and SynapticSearch share a single implementation.
 *
 * @param indices       Range of candidate indices into @p blocks.  Any index
 *                      out of range of @p blocks is skipped.
 * @param target_fp     Primary fingerprint of the target block.
 * @param blocks        All blocks in the brain (read-only).
 * @param analyser      Analyser used for distance computation.
 * @param block_usages  Per-block usage counters (caller-owned).
 * @param params        Search tuning parameters.
 * @return              {best_index, best_score}.  If @p indices is empty,
 *                      returns {0, numeric_limits<double>::max()}.
 */
template <typename IndexRange>
inline std::pair<std::size_t, double> scoreCandidates(const IndexRange& indices,
                                                      const std::vector<double>& target_fp,
                                                      const std::vector<Block>& blocks,
                                                      const port::IAnalyser& analyser,
                                                      const std::vector<double>& block_usages,
                                                      const SearchParams& params) {
    double best_score = std::numeric_limits<double>::max();
    std::size_t best_idx = 0;
    for (const std::size_t i : indices) {
        if (i >= blocks.size()) {
            continue;
        }
        const double usage = (i < block_usages.size()) ? block_usages[i] : 0.0;
        const double score =
            analyser.distance(target_fp, blocks[i].print.mfcc) + (usage * params.usage_weight);
        if (score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }
    return {best_idx, best_score};
}

}  // namespace audio::adapter::search::SearchUtils
