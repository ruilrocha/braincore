#pragma once

#include "AudioPrint.h"
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
    /// Raw audio samples (mono, channel 0 — used for analysis).
    std::vector<double> samples;

    /// Per-channel audio samples (all channels, used for reconstruction).
    std::vector<std::vector<double>> channel_samples;

    // ── Audio prints ───────────────────────────────────────────────────
    /// Fingerprints computed from raw windowed samples.
    AudioPrint print;

    /// Fingerprints computed from amplitude-normalised samples.
    AudioPrint normalised_print;

    // ── Source metadata ────────────────────────────────────────────────
    std::string source_name;  ///< Label / path of the originating sound.

    // ── Video metadata (optional) ──────────────────────────────────────
    /// Present when this block was sourced from a video file.
    std::optional<VideoSegment> video;
};

}  // namespace audio
