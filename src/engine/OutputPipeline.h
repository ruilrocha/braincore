#pragma once

#include "../adapter/effects/OlaBuffer.h"
#include "../domain/Block.h"
#include "../domain/WindowShape.h"
#include "BlockEffectChain.h"
#include "BrainEngineTypes.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace audio {

/**
 * Two-stage audio output pipeline: OLA synthesis → BlockEffectChain.
 *
 * ## Stage 1: OlaBuffer (Overlap-Add)
 * When overlap > 0, consecutive matched blocks are windowed and overlap-summed
 * to produce smooth crossfades.  `push()` accumulates each matched block into
 * the OLA buffer.
 *
 * ## Stage 2: BlockEffectChain
 * Per-block frequency-domain post-processing (e.g. SpectralMorph).
 * Applied to the output of Stage 1 (OLA path) or directly to the raw block
 * samples (non-OLA path).
 *
 * ## Signal chain
 * @code
 *   // After advance():
 *   pipeline.push(matched_idx, matched_block.channel_samples);
 *
 *   // To get output:
 *   pipeline.readInterleaved(matched_idx, brain.blocks(), out_buffer, max_frames);
 * @endcode
 *
 * ## Thread safety
 * `push()` and `read*()` must be called from the same audio thread.
 * `setEffectAmount()` uses relaxed atomics and is safe to call from a UI thread.
 * `addEffect()` and `removeEffect()` must NOT be called while `push()` or
 * `read*()` is running.
 */
class OutputPipeline {
public:
    /**
     * @param block_size  Samples per block (must match Brain's block_size).
     * @param overlap     OLA overlap ratio [0.0, 0.9).  0 = OLA inactive.
     * @param window      Synthesis window shape for OLA.
     */
    OutputPipeline(int block_size, double overlap, WindowShape window);

    // ── Effect management ──────────────────────────────────────────────────

    /**
     * Add a post-processing effect to the end of the effect chain.
     * No-op if an effect of the same type already exists.
     */
    void addEffect(EffectType type, std::shared_ptr<port::IBlockEffect> effect);

    /** Remove an effect from the chain (no-op if absent). */
    void removeEffect(EffectType type) noexcept;

    /**
     * Set the mix amount for an effect [0.0, 1.0].
     * Safe to call from a UI thread (relaxed atomic).
     */
    void setEffectAmount(EffectType type, double amount) noexcept;

    // ── Per-block push/read ────────────────────────────────────────────────

    /**
     * Called after each advance() / advanceInfinite() call.
     * Tracks the matched index and (if OLA is active) accumulates the block
     * into the OLA synthesis buffer.
     */
    void push(std::size_t matched_idx, const std::vector<std::vector<float>>& channel_samples);

    /**
     * Read interleaved multi-channel output for the last matched block.
     *
     * OLA path (overlap > 0): drains the OLA buffer and applies the effect chain.
     * Direct path (overlap == 0): reads from the raw block, applies the effect chain.
     *
     * Must be called with the same index that was passed to the most recent push().
     *
     * @return Number of frames written into @p out_buffer.
     */
    [[nodiscard]] std::size_t readInterleaved(std::size_t index, const std::vector<Block>& blocks,
                                              double* out_buffer, std::size_t max_frames);

    /**
     * Read mono channel-0 output for the last matched block.
     * Applies the effect chain (single-channel).
     * @return Number of samples written into @p out_buffer.
     */
    [[nodiscard]] std::size_t readMono(std::size_t index, const std::vector<Block>& blocks,
                                       double* out_buffer, std::size_t max_count);

    // ── State ──────────────────────────────────────────────────────────────

    /** Frames per advance step (= blockSize when OLA inactive, blockSize*(1−overlap) when active).
     */
    [[nodiscard]] std::size_t stepSize() const noexcept;

    /** True when overlap > 0 and OLA is active. */
    [[nodiscard]] bool olaActive() const noexcept;

    /** Index of the block from the most recent push(), or nullopt if push() was never called. */
    [[nodiscard]] std::optional<std::size_t> lastMatchedIdx() const noexcept {
        return last_matched_idx_;
    }

    /** Reset OLA buffer and clear effect chain feedback. */
    void reset() noexcept;

private:
    adapter::effects::OlaBuffer ola_buffer_;
    BlockEffectChain effects_;

    std::optional<std::size_t> last_matched_idx_;

    /// Scratch buffers — reused to avoid heap allocation on the audio thread.
    std::vector<std::vector<double>> channel_scratch_;  ///< [ch][sample] for OLA + effect chain
};

}  // namespace audio
