#include "PlayHead.h"

#include "Brain.h"

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

std::size_t PlayHead::advance(const TargetAnalysis& target, const SearchParams& params) {
    current_block_idx_ =
        search_->search(target, *brain_, params, current_block_idx_, block_usages_);
    return current_block_idx_;
}

void PlayHead::reset() {
    current_block_idx_ = 0;
    std::fill(block_usages_.begin(), block_usages_.end(), 0.0);
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
