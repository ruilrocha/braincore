#pragma once

#include "../domain/BlockAnalysis.h"

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
 * Silent-block detection: if a matched block has near-zero mel energy the
 * drift path is reseeded rather than converging on silence.
 */
class InfiniteDriftState {
public:
    InfiniteDriftState() = default;

    /**
     * Seed the drift target from a random brain block, adding small noise
     * so each session starts from a distinct position in timbral space.
     * No-op if the brain is empty.
     */
    void initFromNoise(const Brain& brain);

    /**
     * Advance drift after a block was matched.
     *
     * Copies the matched block's fingerprint with a small noise perturbation
     * as the next search target.
     *
     * Reinitialises from noise if the block is silent or the same index has
     * been returned more than 16 times consecutively.
     *
     * @param matched_print  AudioPrint of the matched block.
     * @param matched_idx    Index of the matched block.
     * @param brain          Brain (used for reinit if stuck/silent).
     * @return               true if the state was reseeded this step.
     */
    bool updateFromMatch(const AudioPrint& matched_print, std::size_t matched_idx,
                         const Brain& brain);

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

    static constexpr int kStuckThreshold = 16;
    static constexpr double kSilenceThreshold = 1e-6;
};

}  // namespace audio
