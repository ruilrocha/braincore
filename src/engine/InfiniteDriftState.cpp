#include "InfiniteDriftState.h"

#include "../domain/BlockAnalysis.h"
#include "../domain/Brain.h"
#include "../domain/Random.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace audio {

namespace {

void copyWithNoise(std::vector<float>& dst, const std::vector<float>& src, double noise_range) {
    dst.resize(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        dst[i] = src[i] + static_cast<float>(rng::randomDouble(-noise_range, noise_range));
    }
}

/// Compute mean mel energy (mean of squared mel values) across all brain blocks.
/// Used as the reference scale for the low-energy escape threshold.
double computeMeanMelEnergy(const Brain& brain) {
    const auto& blocks = brain.blocks();
    if (blocks.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (const auto& block : blocks) {
        for (const float v : block.analysis.print.mel) {
            total += static_cast<double>(v) * static_cast<double>(v);
        }
    }
    // Divide by number of blocks (energy per block, not per bin).
    return total / static_cast<double>(blocks.size());
}

}  // namespace

void InfiniteDriftState::initFromNoise(const Brain& brain) {
    if (brain.empty()) {
        return;
    }

    brain_mean_mel_energy_ = computeMeanMelEnergy(brain);
    low_energy_count_ = 0;

    const std::size_t seed_idx = rng::randomIndex(brain.blocks().size());
    const AudioPrint& ref = brain.blocks()[seed_idx].analysis.print;

    auto jitter = [](const std::vector<float>& src) {
        std::vector<float> vec(src.size());
        for (std::size_t i = 0; i < src.size(); ++i) {
            vec[i] = src[i] + static_cast<float>(rng::randomDouble(-0.1, 0.1));
        }
        return vec;
    };

    drift_print_.print.mfcc = jitter(ref.mfcc);
    drift_print_.print.mel = jitter(ref.mel);
    drift_print_.print.spectral = jitter(ref.spectral);
    drift_print_.print.chroma = jitter(ref.chroma);
    drift_print_.normalised_print = drift_print_.print;
}

bool InfiniteDriftState::updateFromMatch(const AudioPrint& matched_print,
                                         const std::size_t matched_idx, const Brain& brain,
                                         const std::vector<double>& block_usages,
                                         const SearchParams& params) {
    // Compute mel energy of the matched block.
    const double energy = [&] {
        double sum = 0.0;
        for (const float v : matched_print.mel) {
            sum += static_cast<double>(v) * static_cast<double>(v);
        }
        return sum;
    }();

    // Low-energy escape: accumulate consecutive low-energy steps and reseed
    // if silence persists.  Brief silence (< kLowEnergyEscapeSteps blocks) is
    // allowed — it can be musically interesting.
    // The threshold is relative to the brain's mean energy so it adapts to any
    // source material (no hardcoded absolute value).
    const double threshold = brain_mean_mel_energy_ * kLowEnergyFraction;
    if (energy < threshold) {
        if (++low_energy_count_ > kLowEnergyEscapeSteps) {
            initFromNoise(brain);
            drift_last_idx_ = matched_idx;
            drift_stuck_count_ = 0;
            return true;
        }
        // Brief silence: still update the target normally so the drift can
        // wander out of the low-energy region on its own.
    } else {
        low_energy_count_ = 0;
    }

    // Next drift target: walk forward through the similarity graph.
    //
    // Select from the precomputed neighbours of the matched block, combining
    // timbral proximity and freshness:
    //
    //   score = (1 − usage_weight) × mel_dist(matched, neighbour)
    //         + usage_weight × (usage[neighbour] / kUsageFactor)
    //         + tiny_noise   (tie-breaking)
    //
    //   usage_weight ≈ 0  → nearest-neighbour walk  (smooth "swimming" feel)
    //   usage_weight ≈ 1  → freshness-driven walk   (novelty, more varied)
    //
    // The previous matched block (drift_last_idx_) is excluded to prevent
    // immediate backtracking (A→B→A oscillation), which is the primary cause
    // of stutter when usage_weight is near zero.
    if (brain.hasIndex()) {
        const auto neighbours = brain.neighbors(matched_idx);
        if (!neighbours.empty()) {
            const double w_usage = std::clamp(params.usage_weight, 0.0, 1.0);
            const double w_dist = 1.0 - w_usage;

            const auto& matched_mel = matched_print.mel;
            const std::size_t mel_n = matched_mel.size();

            std::size_t best = std::numeric_limits<std::size_t>::max();
            double best_score = std::numeric_limits<double>::max();

            for (std::size_t n = 0; n < neighbours.size(); ++n) {
                const std::size_t ni = neighbours[n];
                // Skip the previous matched block to avoid backtracking.
                if (ni == drift_last_idx_) {
                    continue;
                }

                const double usage = (ni < block_usages.size()) ? block_usages[ni] : 0.0;

                double dist = 0.0;
                if (w_dist > 0.0 && mel_n > 0) {
                    const auto& nb_mel = brain.blocks()[ni].analysis.print.mel;
                    const std::size_t n_mel = std::min(mel_n, nb_mel.size());
                    for (std::size_t k = 0; k < n_mel; ++k) {
                        const double d =
                            static_cast<double>(matched_mel[k]) - static_cast<double>(nb_mel[k]);
                        dist += d * d;
                    }
                    dist /= static_cast<double>(n_mel);
                }

                const double score =
                    w_dist * dist + w_usage * (usage / kUsageFactor) + rng::randomDouble(0.0, 1e-9);
                if (score < best_score) {
                    best_score = score;
                    best = n;
                }
            }

            if (best != std::numeric_limits<std::size_t>::max()) {
                const AudioPrint& nb = brain.blocks()[neighbours[best]].analysis.print;
                copyWithNoise(drift_print_.print.mfcc, nb.mfcc, 0.02);
                copyWithNoise(drift_print_.print.mel, nb.mel, 0.02);
                copyWithNoise(drift_print_.print.spectral, nb.spectral, 0.02);
            } else {
                // All neighbours excluded (tiny brain) — fall back to noise on matched.
                copyWithNoise(drift_print_.print.mfcc, matched_print.mfcc, 0.05);
                copyWithNoise(drift_print_.print.mel, matched_print.mel, 0.05);
                copyWithNoise(drift_print_.print.spectral, matched_print.spectral, 0.05);
            }
        } else {
            copyWithNoise(drift_print_.print.mfcc, matched_print.mfcc, 0.05);
            copyWithNoise(drift_print_.print.mel, matched_print.mel, 0.05);
            copyWithNoise(drift_print_.print.spectral, matched_print.spectral, 0.05);
        }
    } else {
        copyWithNoise(drift_print_.print.mfcc, matched_print.mfcc, 0.05);
        copyWithNoise(drift_print_.print.mel, matched_print.mel, 0.05);
        copyWithNoise(drift_print_.print.spectral, matched_print.spectral, 0.05);
    }
    drift_print_.normalised_print = drift_print_.print;

    // Stuck detection: reinit if the same block keeps winning.
    if (matched_idx == drift_last_idx_) {
        ++drift_stuck_count_;
        if (drift_stuck_count_ > kStuckThreshold) {
            initFromNoise(brain);
            drift_stuck_count_ = 0;
            return true;
        }
    } else {
        drift_stuck_count_ = 0;
        drift_last_idx_ = matched_idx;
    }

    return false;
}

void InfiniteDriftState::reset() noexcept {
    drift_print_ = {};
    drift_last_idx_ = 0;
    drift_stuck_count_ = 0;
    brain_mean_mel_energy_ = 0.0;
    low_energy_count_ = 0;
}

}  // namespace audio
