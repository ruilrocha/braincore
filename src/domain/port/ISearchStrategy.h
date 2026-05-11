#pragma once

#include "../Block.h"
#include "../SearchParams.h"

#include <vector>

namespace audio::port {

class IAnalyser;

/**
 * Port: strategy for choosing which brain block best replaces a target block.
 *
 * Implementations live in src/adapter/search/.
 *
 * Design notes:
 *   - `blocks` is const -- Brain is an immutable data container; strategies
 *     must not mutate blocks directly.
 *   - `block_usages` is a separate mutable vector (sized to blocks.size())
 *     owned by the caller (StreamProcessor / SoundProcessor).  Strategies
 *     read usage penalties from it and write selections back into it.
 *   - Strategies that need a SynapseGraph receive it via constructor injection
 *     (see SynapseAwareSearch).  If construction succeeds, the graph is
 *     guaranteed to be valid for the object lifetime.
 */
class ISearchStrategy {
public:
    virtual ~ISearchStrategy() = default;

    /**
     * Select the best block from @p blocks for the given @p target_fp.
     *
     * @param target_fp           Fingerprint of the target block.
     * @param blocks              All blocks in the brain (read-only).
     * @param analyser            Analyser (for distance computation).
     * @param params              Search tuning parameters.
     * @param current_block_index Index of the block returned by the previous call
     *                            (used for stickyness / sequential biasing).
     * @param block_usages        Per-block usage counters (caller-owned, same size
     *                            as @p blocks).  Strategies read and update this.
     * @return                    Index into @p blocks of the chosen block.
     */
    [[nodiscard]] virtual std::size_t search(const std::vector<double>& target_fp,
                                             const std::vector<Block>& blocks,
                                             const IAnalyser& analyser, const SearchParams& params,
                                             std::size_t current_block_index,
                                             std::vector<double>& block_usages) const = 0;
};

}  // namespace audio::port
