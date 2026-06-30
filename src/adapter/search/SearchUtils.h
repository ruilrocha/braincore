#pragma once

#include "../../domain/Block.h"
#include "../../domain/BlockAnalysis.h"
#include "../../domain/SearchParams.h"

#include <algorithm>
#include <cmath>
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
 * Full score for a candidate block: weighted distance + usage penalty + brightness bias.
 *
 * @param target          Target block analysis (raw + normalised fingerprints).
 * @param candidate       Candidate block analysis (raw + normalised fingerprints).
 * @param candidate_usage Current usage counter for the candidate.
 * @param params          Search parameters.
 * @return                Score (lower = better match).
 */
inline double fullScore(const BlockAnalysis& target, const BlockAnalysis& candidate,
                        double candidate_usage, const SearchParams& params) {
    double score =
        weightedDist(target, candidate, params) + (candidate_usage * params.usage_weight);

    if (params.brightness_weight > 0.0) {
        // Mel spectral centroid normalised to [0, 1]:
        //   centroid = sum(i * mel[i]) / sum(mel[i])  / (N - 1)
        // Returns 0.5 when mel is empty or energy is zero (neutral).
        //
        // The penalty is EXPONENTIAL: score × exp(weight × d² × kGain).
        // A multiplicative (1 + weight × d²) factor is bounded at 2× for weight=1,
        // which is too weak — timbral distances can span 1000:1 ratios within a brain.
        // With kGain=10: at weight=1,d=1 the factor is e^10 ≈ 22000×, making
        // off-brightness blocks essentially unselectable.  At weight=0.5,d=1: e^5 ≈ 148×.
        // Weight stays in the natural [0, 1] range: 0=off, 0.5=strong bias, 1=near-pure selection.
        const auto& mel = candidate.normalised_print.mel;
        const std::size_t mel_n = mel.size();
        if (mel_n > 1) {
            double energy = 0.0;
            double weighted = 0.0;
            for (std::size_t i = 0; i < mel_n; ++i) {
                const double v = mel[i];
                energy += v;
                weighted += static_cast<double>(i) * v;
            }
            const double brightness =
                (energy > 1e-12) ? (weighted / energy) / static_cast<double>(mel_n - 1) : 0.5;
            const double d = brightness - params.brightness_target;
            const double bw = std::clamp(params.brightness_weight, 0.0, 1.0);
            static constexpr double kGain = 10.0;
            score *= std::exp(bw * d * d * kGain);
        }
    }

    return score;
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
