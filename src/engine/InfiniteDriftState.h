#pragma once

#include "../domain/BlockAnalysis.h"
#include "../domain/SearchParams.h"

#include <cstddef>
#include <vector>

namespace audio {

class Brain;
struct AudioPrint;

/**
 * State machine for the infinite generative (target-free) mode.
 *
 * Tracks an evolving "target" fingerprint that walks through timbral space.
 * Each step, the brain finds the block most similar to the current target;
 * that block's fingerprint (plus a small noise perturbation) becomes the
 * next target.  This traces a path through the brain's timbral landscape.
 *
 * Stuck detection: if the same block is returned more than 16 consecutive
 * times (can happen with tiny brains or usage_weight == 0), the state is
 * reseeded from a fresh random position.
 *
 * Low-energy escape: consecutive low-energy blocks (silence or near-silence)
 * are tolerated for up to kLowEnergyEscapeSteps steps.  If silence persists
 * beyond that, the drift is reseeded.  The threshold is computed relative to
 * the brain's own mean energy at seed time, so it works across any material.
 * Brief silence is preserved — it can be musically interesting.
 */
class InfiniteDriftState {
public:
    InfiniteDriftState() = default;

    /**
     * Seed the drift target from a random brain block, adding small noise
     * so each session starts from a distinct position in timbral space.
     * Also samples the brain to compute the mean mel energy used as the
     * low-energy escape threshold.  No-op if the brain is empty.
     */
    void initFromNoise(const Brain& brain);

    /**
     * Advance drift after a block was matched.
     *
     * Selects the next drift target from the precomputed K-NN neighbours of
     * @p matched_idx using a blend of timbral proximity and freshness:
     *
     *   score = (1 − usage_weight) × mel_distance(matched, neighbour)
     *         + usage_weight × (usage[neighbour] / kUsageFactor)
     *
     *   - usage_weight ≈ 0 → always walks to the nearest neighbour (smooth "swimming")
     *   - usage_weight ≈ 1 → prefers the freshest neighbour (novelty-directed)
     *
     * The previous matched block is excluded from consideration to prevent
     * immediate backtracking (A→B→A oscillation), which is the primary cause
     * of stutter at low usage_weight.
     *
     * Falls back to matched + noise when no index is built or no eligible
     * neighbours remain (tiny brain).
     *
     * Reinitialises from noise if:
     *  - The same block index has been returned more than kStuckThreshold
     *    consecutive times (stuck in local minimum).
     *  - More than kLowEnergyEscapeSteps consecutive low-energy blocks have
     *    been returned (trapped in a silence cluster).
     *
     * @param matched_print  AudioPrint of the matched block.
     * @param matched_idx    Index of the matched block.
     * @param brain          Brain (used for neighbour lookup and reinit).
     * @param block_usages   Per-block usage counters from PlayHead.
     * @param params         Current search parameters (usage_weight used here).
     * @return               true if the state was reseeded this step.
     */
    bool updateFromMatch(const AudioPrint& matched_print, std::size_t matched_idx,
                         const Brain& brain, const std::vector<double>& block_usages,
                         const SearchParams& params);

    /** The fingerprint to use as the target on the next advance call. */
    [[nodiscard]] const BlockAnalysis& currentTarget() const noexcept { return drift_print_; }

    /** True once initFromNoise() has been called successfully at least once. */
    [[nodiscard]] bool seeded() const noexcept { return !drift_print_.print.mfcc.empty(); }

    /** Reset to the initial unseeded state. */
    void reset() noexcept;

private:
    BlockAnalysis drift_print_;
    std::size_t drift_last_idx_ = 0;
    int drift_stuck_count_ = 0;

    // Low-energy escape state.
    double brain_mean_mel_energy_ = 0.0;  ///< Computed from brain blocks at initFromNoise time.
    int low_energy_count_ = 0;            ///< Consecutive low-energy steps since last reset.

    static constexpr int kStuckThreshold = 16;
    static constexpr int kLowEnergyEscapeSteps = 8;
    /// Fraction of brain mean mel energy below which a block is considered "low energy".
    static constexpr double kLowEnergyFraction = 0.05;
};

}  // namespace audio
