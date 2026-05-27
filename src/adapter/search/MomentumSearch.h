#pragma once

#include "../../domain/Brain.h"
#include "../../domain/port/ISearchStrategy.h"

#include <vector>

namespace audio::adapter::search {

/**
 * Momentum-based search: tracks a velocity vector in fingerprint space.
 *
 * Instead of always snapping to the globally closest block, this strategy
 * maintains a "direction of travel" through the brain's timbral landscape.
 * Each step, the search predicts where it *wants* to go (current position
 * + velocity) and finds the best match near that predicted point.
 *
 * The result is smooth, evolving audio that drifts through the brain's
 * sound palette — like a stream flowing through terrain rather than
 * teleporting between landmarks.
 *
 * Controlled by SearchParams::momentum and SearchParams::momentum_decay:
 *   - momentum [0,1]: how much the velocity influences the search target.
 *   - momentum_decay [0,1]: how quickly the velocity dissipates (1 = never).
 *
 * The velocity is stored as mutable state (the strategy is logically
 * stateful but safe — only one thread drives the search loop).
 */
class MomentumSearch final : public port::ISearchStrategy {
public:
    MomentumSearch() = default;

    [[nodiscard]] std::size_t search(const TargetAnalysis& target, const audio::Brain& brain,
                                     const SearchParams& params, std::size_t current_block_index,
                                     std::vector<double>& block_usages) const override;

private:
    /// Velocity vector in fingerprint space (mutable because search is const).
    mutable std::vector<double> velocity_;
    /// Previous fingerprint position for computing deltas.
    mutable std::vector<double> prev_fp_;
};

}  // namespace audio::adapter::search
