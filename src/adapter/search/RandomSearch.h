#pragma once

#include "../../domain/port/ISearchStrategy.h"

namespace audio::adapter::search {

/**
 * Pure-random search: selects a random block from the brain.
 *
 * Produces extreme experimental / glitch output — no fingerprint comparison
 * at all.  Usage tracking is still applied so the "boredom" parameter can
 * prevent the same block from being selected repeatedly.
 */
class RandomSearch final : public port::ISearchStrategy {
public:
    [[nodiscard]] std::size_t search(
        const std::vector<double>& target_fp,
        std::vector<Block>& blocks,
        const port::IAnalyser& analyser,
        const SearchParams& params,
        std::size_t current_block_index) const override;
};

} // namespace audio::adapter::search

