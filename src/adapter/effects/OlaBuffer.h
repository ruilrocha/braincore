#pragma once

#include "../../domain/WindowShape.h"

#include <cstddef>
#include <vector>

namespace audio::adapter::effects {

/**
 * Overlap-Add (OLA) synthesis buffer.
 *
 * ## What is OLA?
 * Each matched block is multiplied by a synthesis window (a bell-shaped curve that
 * fades in at the start and out at the end), then placed `step` samples later than the
 * previous block in a circular accumulation buffer. Because consecutive blocks overlap,
 * their windowed tails and heads sum together — producing a smooth crossfade rather than
 * a hard cut.
 *
 * At 50 % overlap with a periodic Hann window, the sum of any two adjacent contributions
 * is exactly 1.0 everywhere (perfect reconstruction — no amplitude ripple). Other windows
 * or overlap ratios may produce a small gain variation at the seam, which is an acceptable
 * timbral trade-off (the same trade-off samplebrain accepts).
 *
 * ## Why a circular accumulation buffer (not a SPSC queue)?
 * A SPSC (single-producer, single-consumer) ring buffer is a FIFO: the producer writes
 * N items and the consumer reads N items sequentially, with no overlap between writes.
 *
 * OLA requires a fundamentally different write pattern: each block of `block_size` samples
 * is written starting at `write_pos`, but the tail
 * `[write_pos + step_size, write_pos + block_size)` will also be accumulated by the NEXT
 * block — this overlapping sum IS the crossfade. A FIFO cannot represent this because it
 * has no concept of "add to existing contents at future positions".
 *
 * The circular accumulation buffer keeps a constant read-write gap equal to
 * `block_size − step_size` (the overlap length). Both cursors advance by `step_size`
 * per block, so the gap stays fixed and the buffer never wraps unexpectedly given
 * `buf_size = 2 * block_size`.
 *
 * ## Usage
 *   OlaBuffer ola(block_size, 0.5, WindowShape::Hann);
 *   // per advance():
 *   ola.accumulate(matched_block.channel_samples);
 *   // per getBlockSamplesInterleaved():
 *   std::vector<std::vector<double>> out(nch, std::vector<double>(ola.stepSize()));
 *   ola.read(out);
 *
 * ## Thread safety
 * Not thread-safe. `accumulate()` and `read()` must be called from the same thread.
 * In BrainSession, both advance() (which calls accumulate) and getBlockSamplesInterleaved()
 * (which calls read) are driven from the same Swift streamLoop thread — this is correct.
 * Cross-thread use would require a double-buffer or mutex, which is not currently needed.
 *
 * ## Inactive mode (overlap == 0)
 * When constructed with overlap == 0, `active()` returns false. `accumulate()` is a
 * no-op and `stepSize()` returns block_size. BrainSession falls back to the raw
 * (non-OLA) path in this case.
 */
class OlaBuffer {
public:
    /**
     * Construct and precompute synthesis window coefficients.
     *
     * @param block_size  Samples per block (must match Brain's block_size).
     * @param overlap     [0, 1). 0 = inactive.  0.5 = 50 % overlap (recommended).
     * @param window      Synthesis window shape.  Hann at 50 % overlap gives perfect
     *                    reconstruction; other combinations may have a small gain error.
     */
    OlaBuffer(std::size_t block_size, double overlap, WindowShape window);

    /**
     * Window + OLA-accumulate one block into the circular buffer.
     *
     * Must be called exactly once per advance() / advanceInfinite() call.
     * No-op when inactive (overlap == 0).
     *
     * Lazy-allocates the circular buffers on the first call (channel count is not
     * known at construction time).
     *
     * @param channel_samples  Per-channel raw samples [ch][sample].  Each channel
     *                         must have at least block_size samples.
     */
    void accumulate(const std::vector<std::vector<float>>& channel_samples);

    /**
     * Read step_size samples per channel from the circular buffer and zero the
     * consumed region.
     *
     * @param out  Output buffer [ch][sample].  Must be pre-sized to
     *             [channels][stepSize()].  Resized if necessary.
     * @return     Number of frames written (= stepSize()).
     */
    std::size_t read(std::vector<std::vector<double>>& out);

    /** Zero all accumulation buffers and reset read/write cursors. */
    void resetBuffer();

    /** Samples per advance step.  = block_size * (1 − overlap), min 1. */
    [[nodiscard]] std::size_t stepSize() const noexcept { return step_size_; }

    /** True when overlap > 0 (OLA is active). */
    [[nodiscard]] bool active() const noexcept { return overlap_ > 0.0; }

private:
    std::size_t block_size_;
    double overlap_;
    std::size_t step_size_;                      ///< = max(1, block_size * (1 − overlap))
    std::size_t buf_size_;                       ///< = block_size * 2  (circular buffer capacity)

    std::vector<double> window_coeffs_;          ///< Precomputed, length = block_size.
    std::vector<std::vector<double>> buffers_;   ///< Accum buffers [ch][buf_size].
    std::vector<std::vector<double>> read_buf_;  ///< Scratch output  [ch][step_size].
    std::size_t read_pos_ = 0;
    std::size_t write_pos_ = 0;
};

}  // namespace audio::adapter::effects
