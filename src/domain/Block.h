#pragma once

#include "BlockAnalysis.h"
#include "VideoSegment.h"

#include <optional>
#include <string>
#include <vector>

namespace audio {

/**
 * A single fixed-length audio block with its audio prints.
 *
 * Audio prints:
 *   - print:            Computed from raw windowed samples.
 *   - normalised_print: Computed from DC-removed, peak-scaled samples
 *                       (amplitude-invariant matching via n_ratio).
 *
 * Usage tracking and synapse data are deliberately absent — they are
 * per-stream concerns managed outside the domain (see StreamProcessor
 * and SynapseGraph respectively).
 */
struct Block {
    // ── Audio data ─────────────────────────────────────────────────────
    /// Per-channel audio samples stored as float32 to halve memory vs double.
    /// channel_samples[0] is the mono reference used for fingerprinting.
    std::vector<std::vector<float>> channel_samples;

    // ── Audio prints ───────────────────────────────────────────────────
    /// Complete fingerprint analysis for this block (raw + normalised).
    BlockAnalysis analysis;

    // ── Source metadata ────────────────────────────────────────────────
    std::string source_name;  ///< Label / path of the originating sound.

    // ── Video metadata (optional) ──────────────────────────────────────
    /// Present when this block was sourced from a video file.
    std::optional<VideoSegment> video;
};

}  // namespace audio
