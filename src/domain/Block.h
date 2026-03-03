#pragma once

#include <string>
#include <vector>

namespace audio {

/**
 * A single fixed-length audio block with its fingerprints, usage tracking,
 * and optional pre-computed similarity graph (synapses).
 */
struct Block {
    // ── Audio data ─────────────────────────────────────────────────────
    /// Raw audio samples (mono, channel 0 — used for fingerprinting).
    std::vector<double> samples;

    /// Per-channel audio samples (all channels, used for reconstruction).
    std::vector<std::vector<double>> channel_samples;

    // ── Fingerprints (raw) ─────────────────────────────────────────────
    /// Primary fingerprint computed from the raw (un-normalised) samples.
    std::vector<double> fingerprint;

    /// Secondary fingerprint computed from raw samples.
    /// Enables the blend_ratio knob between two complementary representations.
    std::vector<double> secondary_fingerprint;

    // ── Fingerprints (normalised) ──────────────────────────────────────
    /// Primary fingerprint computed from DC-removed, peak-scaled samples.
    /// Used for amplitude-invariant comparison via the n_ratio parameter.
    std::vector<double> normalised_fingerprint;

    /// Secondary fingerprint from normalised samples.
    std::vector<double> normalised_secondary_fingerprint;

    // ── Spectral metadata ──────────────────────────────────────────────
    /// Dominant frequency of the raw block (Hz).
    double dominant_freq = 0.0;

    // ── Source metadata ────────────────────────────────────────────────
    std::string source_name;  ///< Label / path of the originating sound.

    // ── Usage tracking ─────────────────────────────────────────────────
    /// Usage counter — incremented each time this block is selected.
    /// Search strategies penalise high-usage blocks to promote variety.
    ///   - The weight on this penalty is called "novelty".
    ///   - The decay rate is called "boredom".
    double usage = 0.0;

    // ── Synapse graph ──────────────────────────────────────────────────
    /// Pre-computed list of indices to the most similar blocks (synapses).
    /// Populated by Brain::buildSynapses().
    std::vector<std::size_t> synapses;
};

} // namespace audio
