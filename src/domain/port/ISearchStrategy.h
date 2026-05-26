#pragma once

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
 *     owned by the caller (PlayHead / SoundProcessor).  Strategies read
 *     usage penalties and write selections back into it.
 *   - All strategies receive the full Brain, giving access to blocks,
 *     analyser, and the optional nearest-neighbour index.  A strategy
 *     may use `brain.index()` to access precomputed synapses or issue
 *     dynamic O(log N) queries — or ignore the index entirely.
 *     Construction injection of the index is not required.
 */
class ISearchStrategy {
public:
    virtual ~ISearchStrategy() = default;

    /**
     * Select the best block from the brain for the given @p target_fp.
     *
     * @param target_fp           Fingerprint of the target block.
     * @param brain               The brain (blocks, analyser, optional index).
     * @param params              Search tuning parameters.
     * @param current_block_index Index returned by the previous call (used for
     *                            stickyness / sequential biasing).
     * @param block_usages        Per-block usage counters (caller-owned, same
     *                            size as brain.size()).  Strategies read and
     *                            update this.
     * @return                    Index into brain.blocks() of the chosen block.
     */
    [[nodiscard]] virtual std::size_t search(const std::vector<double>& target_fp,
                                             const audio::Brain& brain, const SearchParams& params,
                                             std::size_t current_block_index,
                                             std::vector<double>& block_usages) const = 0;
};

}  // namespace audio::port
