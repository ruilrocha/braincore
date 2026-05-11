#pragma once

#include "AudioPrint.h"
#include "VideoSegment.h"

#include <optional>
#include <string>
#include <vector>

namespace audio {

/**
 * A single fixed-length audio block with its audio prints, usage tracking,
 * and optional pre-computed similarity graph (synapses).
 *
 * Audio prints:
 *   - print:            Computed from raw windowed samples.
 *   - normalised_print: Computed from DC-removed, peak-scaled samples
 *                       (amplitude-invariant matching via n_ratio).
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

    // ── Usage tracking ─────────────────────────────────────────────────
    /// Usage counter — incremented each time this block is selected.
    double usage = 0.0;

    // ── Synapse graph ──────────────────────────────────────────────────
    /// Pre-computed list of indices to the most similar blocks.
    std::vector<std::size_t> synapses;
};

}  // namespace audio
