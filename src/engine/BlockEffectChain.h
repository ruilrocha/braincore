#pragma once

#include "../domain/port/IBlockEffect.h"
#include "BrainEngineTypes.h"

#include <atomic>
#include <memory>
#include <vector>

namespace audio {

/**
 * A simple ordered chain of IBlockEffect adapters applied per audio block.
 *
 * Each effect slot holds:
 *   - The IBlockEffect implementation (e.g. SpectralMorph)
 *   - A relaxed atomic amount [0.0, 1.0] (safe for UI→audio thread handoff)
 *   - Per-channel feedback buffers for IIR-style stateful effects
 *
 * Calling apply() processes all active effects in insertion order:
 *   output[ch] = effect.apply(feedback[ch], input[ch], amount)
 * then stores output[ch] back into the feedback for the next block.
 * This produces a reverb-like "smearing" when the amount is high.
 *
 * ## Thread safety
 * `setAmount()` uses relaxed atomics — safe to call from a UI thread while
 * `apply()` runs on the audio thread (stale-by-one-block is acceptable).
 * `add()` and `remove()` must NOT be called while `apply()` is running.
 */
class BlockEffectChain {
public:
    BlockEffectChain() = default;

    /**
     * Add an effect to the end of the chain.
     * No-op if an effect with the same type is already present.
     */
    void add(EffectType type, std::shared_ptr<port::IBlockEffect> effect);

    /**
     * Remove the effect with the given type from the chain.
     * No-op if not present. Clears the associated feedback buffers.
     */
    void remove(EffectType type) noexcept;

    /**
     * Set the mix amount for an effect [0.0, 1.0].
     * Safe to call from any thread (relaxed atomic store).
     */
    void setAmount(EffectType type, double amount) noexcept;

    /** True if an effect of the given type is present in the chain. */
    [[nodiscard]] bool has(EffectType type) const noexcept;

    /** True if the chain contains at least one effect. */
    [[nodiscard]] bool empty() const noexcept { return slots_.empty(); }

    /**
     * Apply all effects in order to @p channels (in-place, multi-channel).
     *
     * Each effect is applied per channel using the stored per-channel
     * feedback as the "previous" block (IIR state).  After applying,
     * the output replaces the feedback for the next call.
     *
     * @param channels  [channel][sample] double buffer, modified in-place.
     */
    void apply(std::vector<std::vector<double>>& channels);

    /** Clear all feedback buffers (call on reset / source change). */
    void clearFeedback() noexcept;

private:
    struct EffectSlot {
        EffectType type;
        std::shared_ptr<port::IBlockEffect> effect;
        std::atomic<double> amount{0.0};
        std::vector<std::vector<double>> feedback;  ///< [channel][sample]
    };

    // unique_ptr so slots are stable in memory despite vector reallocation
    // (required because std::atomic is not moveable in pre-C++20 ABI).
    std::vector<std::unique_ptr<EffectSlot>> slots_;
};

}  // namespace audio
