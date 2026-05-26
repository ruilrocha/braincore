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

// ── Blended distance computation ───────────────────────────────────────

namespace detail {

/// Sum of squared differences over a range [start, end).
inline double ssd(const std::vector<double>& a, const std::vector<double>& b, std::size_t start,
                  std::size_t end) {
    double acc = 0.0;
    const std::size_t lim = std::min({end, a.size(), b.size()});
    for (std::size_t i = start; i < lim; ++i) {
        const double d = a[i] - b[i];
        acc += d * d;
    }
    return acc;
}

/// Linear blend: a*(1-t) + b*t.
inline double blend(double a, double b, double t) {
    return (a * (1.0 - t)) + (b * t);
}

/// Compute distance between one pair of primary+secondary fingerprints.
inline double layerDistance(const std::vector<double>& primary_a,
                            const std::vector<double>& secondary_a,
                            const std::vector<double>& primary_b,
                            const std::vector<double>& secondary_b, const SearchParams& params) {
    const auto sec_start = static_cast<std::size_t>(std::max(0, params.spectral_start));
    const auto sec_end = static_cast<std::size_t>(std::max(0, params.spectral_end));

    if (params.blend_ratio >= 1.0) {
        // Pure primary.
        const std::size_t n =
            std::max(std::min(primary_a.size(), primary_b.size()), static_cast<std::size_t>(1));
        return ssd(primary_a, primary_b, 0, n) / static_cast<double>(n);
    }
    if (params.blend_ratio <= 0.0) {
        constexpr double kSecondaryBias = 200.0;
        // Pure secondary.
        const std::size_t n = std::max(secondary_a.size(), static_cast<std::size_t>(1));
        return ssd(secondary_a, secondary_b, sec_start, sec_end) / static_cast<double>(n) *
               kSecondaryBias;
    }
    // Blend both.
    const std::size_t sn = std::max(secondary_a.size(), static_cast<std::size_t>(1));
    const std::size_t pn = std::max(primary_a.size(), static_cast<std::size_t>(1));
    const double sec_d =
        ssd(secondary_a, secondary_b, sec_start, sec_end) / static_cast<double>(sn);
    const double pri_d = ssd(primary_a, primary_b, 0, pn) / static_cast<double>(pn);
    return blend(sec_d, pri_d, params.blend_ratio);
}

}  // namespace detail

/**
 * Compute the full blended distance between a target block and a candidate
 * brain block, considering:
 *   - primary vs. secondary fingerprint blend (blend_ratio)
 *   - raw vs. normalised fingerprint blend (n_ratio)
 *   - usage penalty (novelty), read from @p block_usages
 *
 * @return Combined distance score (lower = better match).
 */
inline double fullScore(const Block& target, const Block& candidate,
                        const std::vector<double>& block_usages, const std::size_t candidate_idx,
                        const SearchParams& params) {
    // Raw comparison (MFCC + spectral blend).
    double raw_dist = detail::layerDistance(target.print.mfcc, target.print.spectral,
                                            candidate.print.mfcc, candidate.print.spectral, params);

    // Normalised comparison (if n_ratio > 0).
    if (params.n_ratio > 0.0) {
        double norm_dist = detail::layerDistance(
            target.normalised_print.mfcc, target.normalised_print.spectral,
            candidate.normalised_print.mfcc, candidate.normalised_print.spectral, params);
        raw_dist = detail::blend(raw_dist, norm_dist, params.n_ratio);
    }

    // Mel filter-bank blend (if mel_weight > 0).
    if (params.mel_weight > 0.0) {
        const auto mel_n = std::max(target.print.mel.size(), std::size_t{1});
        const double mel_dist = detail::ssd(target.print.mel, candidate.print.mel, 0, mel_n) /
                                static_cast<double>(mel_n);
        raw_dist = detail::blend(raw_dist, mel_dist, params.mel_weight);
    }

    // Usage penalty ("novelty").
    if (candidate_idx < block_usages.size()) {
        raw_dist += block_usages[candidate_idx] * params.usage_weight;
    }
    return raw_dist;
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
