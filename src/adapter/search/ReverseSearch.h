#pragma once

#include "../../domain/port/ISearchStrategy.h"

namespace audio::adapter::search {

/**
 * Reverse / "furthest-match" search.
 *
 * Instead of the closest block, picks the one with the *largest* fingerprint
 * distance.  This produces extreme audio mangling — every target block is
 * replaced by the most dissimilar source block.
 */
class ReverseSearch final : public port::ISearchStrategy {
public:
    [[nodiscard]] std::size_t search(
        const std::vector<double>& target_fp,
        std::vector<Block>& blocks,
        const port::IAnalyser& analyser,
        const SearchParams& params,
        std::size_t current_block_index) const override;
};

} // namespace audio::adapter::search

