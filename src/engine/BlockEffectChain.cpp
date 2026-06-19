#include "BlockEffectChain.h"

#include <algorithm>

namespace audio {

void BlockEffectChain::add(const EffectType type, std::shared_ptr<port::IBlockEffect> effect) {
    if (has(type)) {
        return;
    }
    auto slot = std::make_unique<EffectSlot>();
    slot->type = type;
    slot->effect = std::move(effect);
    slots_.push_back(std::move(slot));
}

void BlockEffectChain::remove(const EffectType type) noexcept {
    slots_.erase(
        std::remove_if(slots_.begin(), slots_.end(),
                       [type](const std::unique_ptr<EffectSlot>& s) { return s->type == type; }),
        slots_.end());
}

void BlockEffectChain::setAmount(const EffectType type, const double amount) noexcept {
    for (auto& slot : slots_) {
        if (slot->type == type) {
            slot->amount.store(amount, std::memory_order_relaxed);
            return;
        }
    }
}

bool BlockEffectChain::has(const EffectType type) const noexcept {
    for (const auto& slot : slots_) {
        if (slot->type == type) {
            return true;
        }
    }
    return false;
}

void BlockEffectChain::apply(std::vector<std::vector<double>>& channels) {
    if (channels.empty() || slots_.empty()) {
        return;
    }
    const std::size_t nch = channels.size();

    for (auto& slot : slots_) {
        const double amount = slot->amount.load(std::memory_order_relaxed);

        // Ensure feedback is sized for current channel count.
        if (slot->feedback.size() < nch) {
            slot->feedback.resize(nch);
        }

        for (std::size_t ch = 0; ch < nch; ++ch) {
            const auto& curr = channels[ch];
            const auto& prev = slot->feedback[ch].empty() ? curr : slot->feedback[ch];
            auto morphed = slot->effect->apply(prev, curr, amount);
            slot->feedback[ch] = morphed;
            channels[ch] = std::move(morphed);
        }
    }
}

void BlockEffectChain::clearFeedback() noexcept {
    for (auto& slot : slots_) {
        slot->feedback.clear();
    }
}

}  // namespace audio
