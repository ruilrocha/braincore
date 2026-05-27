#pragma once

#include "../AudioPrint.h"
#include "../Block.h"
#include "../SearchParams.h"

#include <cstddef>
#include <vector>

namespace audio {
class Brain;  // forward-declared; implementations must #include Brain.h
}  // namespace audio

namespace audio::port {

/**
 * Port: strategy for choosing which brain block best replaces a target block.
 *
 * Implementations live in src/adapter/search/.
 *
 * Design notes:
 *   - `brain` is passed by const reference — Brain is an immutable data
 *     container; strategies must not mutate it.
 *   - `block_usages` is a separate mutable vector (sized to brain.size())
 *     owned by the caller (PlayHead).  Strategies read usage penalties and
 *     write selections back into it.
 *   - `target` carries both raw and normalised prints so strategies can
 *     apply `SearchParams::n_ratio` blending correctly.
 */
class ISearchStrategy {
public:
    virtual ~ISearchStrategy() = default;

    /**
     * Select the best block from the brain for the given @p target.
     *
     * @param target              Raw + normalised fingerprints of the target block.
     * @param brain               The brain (blocks, analyser, optional index).
     * @param params              Search tuning parameters.
     * @param current_block_index Index returned by the previous call (used for
     *                            stickyness / sequential biasing).
     * @param block_usages        Per-block usage counters (caller-owned, same
     *                            size as brain.size()).  Strategies read and
     *                            update this.
     * @return                    Index into brain.blocks() of the chosen block.
     */
    [[nodiscard]] virtual std::size_t search(const TargetAnalysis& target,
                                             const audio::Brain& brain, const SearchParams& params,
                                             std::size_t current_block_index,
                                             std::vector<double>& block_usages) const = 0;
};

}  // namespace audio::port
