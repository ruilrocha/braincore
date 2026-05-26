#pragma once

#include "../domain/Block.h"
#include "../domain/SearchParams.h"

#include <cstddef>
#include <vector>

namespace audio::usecase {

/**
 * The unit of work that flows through the audio pipeline.
 *
 * One BlockContext is created per audio block and passed through each
 * IBlockStage in order.  Stages read from and write into this structure.
 *
 * Ownership: all vectors are owned by the context for the duration of one
 * pipeline run.  Stages may move from them but must leave the context in a
 * valid (possibly empty) state.
 */
struct BlockContext {
    // ── Input ─────────────────────────────────────────────────────────────

    /// Position of this block within the target (frame index, not sample offset).
    std::size_t block_index = 0;

    /// Audio clock position at the start of this block (seconds).
    /// Set by the pipeline runner before the first stage runs.
    double audio_start_sec = 0.0;

    /// Live parameter snapshot for this block (set before the first stage).
    SearchParams params;

    // ── Analysis stage output ─────────────────────────────────────────────

    /// Primary fingerprint computed from the windowed target block.
    /// Populated by AnalysisStage.
    std::vector<double> fingerprint;

    // ── Search stage output ───────────────────────────────────────────────

    /// Index into brain.blocks() of the matched block.
    /// Populated by SearchStage.
    std::size_t match_idx = 0;

    /// Non-owning pointer into brain.blocks()[match_idx].
    /// Valid as long as the Brain is alive (guaranteed for the block's lifetime).
    const Block* match = nullptr;

    // ── Synthesis stage output ────────────────────────────────────────────

    /// Per-channel processed audio samples (post-effects, post-alpha-blend).
    /// Populated by SynthesisStage.  channel_outputs[ch] has block_size samples.
    std::vector<std::vector<double>> channel_outputs;
};

}  // namespace audio::usecase
