#pragma once

#include "VideoSegment.h"

#include <optional>
#include <string>
#include <vector>

namespace audio {

/**
 * A single fixed-length audio block with its audio print vectors,
 * usage tracking, and optional pre-computed similarity graph (synapses).
 *
 * Audio print fields:
 *   - mfcc: MFCC coefficients capturing timbral shape.
 *   - spectral: FFT magnitude bins capturing harmonic/noise detail.
 *   - normalised_*: same but computed from amplitude-normalised samples.
 */
struct Block {
    // ── Audio data ─────────────────────────────────────────────────────
    /// Raw audio samples (mono, channel 0 — used for analysis).
    std::vector<double> samples;

    /// Per-channel audio samples (all channels, used for reconstruction).
    std::vector<std::vector<double>> channel_samples;

    // ── Audio Print (raw) ──────────────────────────────────────────────
    /// MFCC coefficients — timbral envelope of the block.
    std::vector<double> mfcc;

    /// FFT magnitude bins — spectral detail of the block.
    /// Enables blend_ratio knob between timbral and spectral matching.
    std::vector<double> spectral;

    // ── Audio Print (normalised) ───────────────────────────────────────
    /// MFCC from DC-removed, peak-scaled samples (amplitude-invariant).
    std::vector<double> normalised_mfcc;

    /// Spectral from normalised samples.
    std::vector<double> normalised_spectral;

    // ── Spectral metadata ──────────────────────────────────────────────
    /// Dominant frequency of the raw block (Hz).
    double dominant_freq = 0.0;

    // ── Source metadata ────────────────────────────────────────────────
    std::string source_name;  ///< Label / path of the originating sound.

    // ── Video metadata (optional) ──────────────────────────────────────
    /// Present when this block was sourced from a video file.
    /// Identifies the time range in the source video that corresponds to
    /// this audio block. std::nullopt for audio-only sources (→ black frame).
    std::optional<VideoSegment> video;

    // ── Usage tracking ─────────────────────────────────────────────────
    /// Usage counter — incremented each time this block is selected.
    /// Search strategies penalise high-usage blocks to promote variety.
    double usage = 0.0;

    // ── Synapse graph ──────────────────────────────────────────────────
    /// Pre-computed list of indices to the most similar blocks (synapses).
    /// Populated by Brain::buildSynapses().
    std::vector<std::size_t> synapses;
};

}  // namespace audio
