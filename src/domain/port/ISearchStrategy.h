#pragma once

#include <vector>
#include "../Block.h"
#include "../SearchParams.h"

namespace audio::port {

class IAnalyser;

/**
 * Port: strategy for choosing which brain block best replaces a target block.
 *
 * Implementations live in src/adapter/search/.
 */
class ISearchStrategy {
public:
    virtual ~ISearchStrategy() = default;

    /**
     * Select the best block from @p blocks for the given @p target_fp.
     *
     * @param target_fp           Fingerprint of the target block.
     * @param blocks              All blocks in the brain.
     * @param analyser            Analyser (for distance computation).
     * @param params              Search tuning parameters.
     * @param current_block_index Index of the block returned by the previous call
     *                            (used for stickyness / sequential biasing).
     * @return                    Index into @p blocks of the chosen block.
     */
    [[nodiscard]] virtual std::size_t search(
        const std::vector<double>& target_fp,
        std::vector<Block>& blocks,
        const IAnalyser& analyser,
        const SearchParams& params,
        std::size_t current_block_index) const = 0;

    /**
     * Whether this strategy requires the synapse graph to be pre-built.
     * Used by Brain::setSearchStrategy() to lazily build synapses on demand.
     */
    [[nodiscard]] virtual bool requiresSynapses() const { return false; }
};

} // namespace audio::port
