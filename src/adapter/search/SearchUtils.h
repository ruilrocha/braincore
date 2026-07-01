#pragma once

#include "../../domain/Block.h"
#include "../../domain/BlockAnalysis.h"
#include "../../domain/SearchParams.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <span>
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

/// Sum of squared differences over a span — contiguous float, auto-vectorizable.
/// Double accumulator preserves numeric precision.
inline double ssdSpan(std::span<const float> a, std::span<const float> b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n == 0) {
        return 0.0;
    }
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        acc += d * d;
    }
    return acc / static_cast<double>(n);
}

/// SSD over a sub-range of two spans.
inline double ssdSpanRange(std::span<const float> a, std::span<const float> b, std::size_t start,
                           std::size_t end) {
    const std::size_t lim = std::min({end, a.size(), b.size()});
    if (lim <= start) {
        return 0.0;
    }
    return ssdSpan(a.subspan(start, lim - start), b.subspan(start, lim - start));
}

/// Convenience wrappers for vector-backed fingerprints.
inline double ssd(const std::vector<float>& a, const std::vector<float>& b, std::size_t start,
                  std::size_t end) {
    return ssdSpanRange(a, b, start, end);
}

inline double ssdFull(const std::vector<float>& a, const std::vector<float>& b) {
    return ssdSpan(a, b);
}

/// Linear blend: a*(1−t) + b*t.
inline double blend(double a, double b, double t) {
    return (a * (1.0 - t)) + (b * t);
}

/**
 * Normalised weights precomputed once per search iteration.
 *
 * Hoisting the per-weight division out of the per-candidate inner loop avoids
 * N × (4 divisions + 4 comparisons) for an N-block scan.
 */
struct NormWeights {
    double mfcc = 0.0;
    double mel = 0.0;
    double spectral = 0.0;
    double chroma = 0.0;
    bool mfcc_fallback = false;  ///< True when all weights are zero → pure MFCC.

    static NormWeights from(const SearchParams& params) noexcept {
        NormWeights w;
        const double wm = std::max(0.0, params.mfcc_weight);
        const double wl = std::max(0.0, params.mel_weight);
        const double ws = std::max(0.0, params.spectral_weight);
        const double wc = std::max(0.0, params.chroma_weight);
        const double total = wm + wl + ws + wc;
        if (total < 1e-12) {
            w.mfcc_fallback = true;
            return w;
        }
        const double inv = 1.0 / total;
        w.mfcc = wm * inv;
        w.mel = wl * inv;
        w.spectral = ws * inv;
        w.chroma = wc * inv;
        return w;
    }
};

/**
 * Weighted multi-feature distance between two AudioPrint bundles.
 *
 * Accepts precomputed normalized weights — call NormWeights::from(params) once
 * before a scan loop and pass the result here to avoid repeated weight normalization.
 */
inline double weightedDistanceW(const AudioPrint& a, const AudioPrint& b, const NormWeights& w,
                                const SearchParams& params) {
    if (w.mfcc_fallback) {
        return ssdFull(a.mfcc, b.mfcc);
    }
    double score = 0.0;
    if (w.mfcc > 0.0) {
        score += w.mfcc * ssdFull(a.mfcc, b.mfcc);
    }
    if (w.mel > 0.0) {
        score += w.mel * ssdFull(a.mel, b.mel);
    }
    if (w.spectral > 0.0) {
        const auto s0 = static_cast<std::size_t>(std::max(0, params.spectral_start));
        const auto s1 = static_cast<std::size_t>(std::max(0, params.spectral_end));
        score += w.spectral * ssd(a.spectral, b.spectral, s0, s1);
    }
    if (w.chroma > 0.0) {
        score += w.chroma * ssdFull(a.chroma, b.chroma);
    }
    return score;
}

/**
 * Weighted multi-feature distance between two AudioPrint bundles.
 * Computes NormWeights inline — use weightedDistanceW with a precomputed NormWeights
 * when calling inside a scan loop.
 */
inline double weightedDistance(const AudioPrint& a, const AudioPrint& b,
                               const SearchParams& params) {
    return weightedDistanceW(a, b, NormWeights::from(params), params);
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
