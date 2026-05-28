#pragma once

#include "../BlockAnalysis.h"
#include "../SearchContext.h"

#include <cstddef>

namespace audio::port {

/**
 * Port: strategy for choosing which brain block best replaces a target block.
 *
 * Implementations live in src/adapter/search/.
 *
 * Design notes:
 *   - All inputs are bundled in `SearchContext` so the signature remains
 *     stable as new contextual fields are added.
 *   - `ctx.brain` is passed by const reference — Brain is an immutable data
 *     container; strategies must not mutate it.
 *   - `ctx.block_usages` is a mutable vector (sized to brain.size()) owned
 *     by the caller (PlayHead).  Strategies read usage penalties and write
 *     selections back into it.
 *   - `ctx.target` carries both raw and normalised prints so strategies can
 *     apply `SearchParams::n_ratio` blending correctly.
 */
class ISearchStrategy {
public:
    virtual ~ISearchStrategy() = default;

    /**
     * Select the best block from the brain for the given target.
     *
     * @param ctx  All search inputs (brain, target, params, position, usages).
     * @return     Index into ctx.brain.blocks() of the chosen block.
     */
    [[nodiscard]] virtual std::size_t search(const SearchContext& ctx) const = 0;
};

}  // namespace audio::port
