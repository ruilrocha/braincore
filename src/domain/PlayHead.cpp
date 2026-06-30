#include "PlayHead.h"

#include "Brain.h"
#include "SearchContext.h"

#include <algorithm>
#include <utility>

namespace audio {

PlayHead::PlayHead(std::shared_ptr<const Brain> brain,
                   std::shared_ptr<port::ISearchStrategy> search)
    : brain_(std::move(brain)), search_(std::move(search)) {
    if (brain_) {
        block_usages_.assign(brain_->size(), 0.0);
    }
}

std::size_t PlayHead::advance(const BlockAnalysis& target, const SearchParams& params) {
    const SearchContext ctx{.brain = *brain_,
                            .target = target,
                            .params = params,
                            .current_block_index = current_block_idx_,
                            .block_usages = block_usages_};
    current_block_idx_ = search_->search(ctx);
    return current_block_idx_;
}

void PlayHead::reset() {
    current_block_idx_ = 0;
    std::ranges::fill(block_usages_, 0.0);
}

void PlayHead::rebind(std::shared_ptr<const Brain> brain,
                      std::shared_ptr<port::ISearchStrategy> search) {
    brain_ = std::move(brain);
    search_ = std::move(search);
    current_block_idx_ = 0;
    block_usages_.assign(brain_ ? brain_->size() : 0, 0.0);
}

void PlayHead::depleteUsages(const double factor) {
    for (auto& usage : block_usages_) {
        usage *= factor;
    }
}

}  // namespace audio
