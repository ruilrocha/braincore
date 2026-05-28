#pragma once

#include "../../domain/Brain.h"
#include "../../domain/SearchContext.h"
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

    [[nodiscard]] std::size_t search(const SearchContext& ctx) const override;

private:
    mutable std::vector<double> velocity_;
    mutable std::vector<double> prev_fp_;
};

}  // namespace audio::adapter::search
