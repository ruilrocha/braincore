#include "RandomSearch.h"

#include <cstdlib>

#include "SearchUtils.h"

namespace audio::adapter::search {

std::size_t RandomSearch::search(
    const std::vector<double>& /*target_fp*/,
    std::vector<Block>& blocks,
    const port::IAnalyser& /*analyser*/,
    const SearchParams& params,
    const std::size_t /*current_block_index*/) const {

    if (blocks.empty()) return 0;

    // Pure random selection — no fingerprint comparison.
    const std::size_t idx =
        static_cast<std::size_t>(std::rand()) % blocks.size();

    SearchUtils::applyUsage(blocks, idx, params.usage_falloff);

    return idx;
}

} // namespace audio::adapter::search

