#pragma once

namespace audio {

/// Factor by which a block's usage counter is incremented when selected.
/// Centralised here so all search strategies share the same constant.
constexpr double kUsageFactor = 1000.0;

/**
 * Value object holding all parameters that control how the brain searches
 * for matching blocks and how the output is reconstructed.
 *
 * Every field is designed to be bound to a UI slider/knob in a future GUI.
 *
 * Key concepts:
 *   - "novelty"        = usage_weight   — penalises re-used blocks
 *   - "boredom"        = usage_falloff  — how fast blocks become available again
 *   - Feature weights  — per-feature contribution sliders (mfcc, mel, spectral, chroma, pitch).
 *                        Weights are normalised internally so their sum = 1.0, so each slider
 *                        can be thought of as "percentage contribution" of that feature.
 *   - "n_ratio"        — blend between raw and normalised fingerprint comparison
 */
struct SearchParams {
    // ── Blend & reconstruction ─────────────────────────────────────────

    /// Cross-fade blend factor: 1.0 = full source replacement, 0.0 = keep target.
    double alpha = 1.0;

    /// Stickyness [0.0, 1.0]: bias toward choosing the *next* sequential block
    /// instead of the globally closest one, for temporal coherence.
    double stickyness = 0.0;

    /// Block overlap in samples (0 = no overlap).
    /// Overlapping blocks produce smoother spectral transitions.
    int overlap = 0;

    // ── Usage tracking ("novelty" & "boredom") ─────────────────────────

    /// "Boredom": usage falloff rate [0.0, 1.0].
    double usage_falloff = 1.0;

    /// "Novelty": weight applied to usage when scoring candidates [0.0, 1.0].
    double usage_weight = 0.0;

    // ── Comparison weights ─────────────────────────────────────────────
    //
    // Per-feature contribution weights [0.0, 1.0].  The scoring function
    // normalises these so they sum to 1.0, so each slider represents a
    // percentage contribution of that feature to the total distance score.
    // Setting all to 0.0 defaults to pure MFCC.
    //
    // Features:
    //   mfcc_weight     — timbral shape (compact, robust, amplitude-independent)
    //   mel_weight      — spectral envelope (more detail than MFCC, less than spectral)
    //   spectral_weight — FFT magnitude bins (high resolution, amplitude-sensitive)
    //   chroma_weight   — pitch-class profile (harmonic content, octave-independent)

    /// MFCC timbral shape contribution [0.0, 1.0]. Default 1.0 (pure MFCC baseline).
    double mfcc_weight = 1.0;

    /// Mel filter-bank envelope contribution [0.0, 1.0].
    double mel_weight = 0.0;

    /// FFT magnitude bins contribution [0.0, 1.0].
    double spectral_weight = 0.0;

    /// Pitch-class (chroma) contribution [0.0, 1.0].
    /// Matches blocks by harmonic/key content, independent of octave and amplitude.
    double chroma_weight = 0.0;

    /// Raw-vs-normalised comparison ratio [0.0, 1.0]:
    ///   0.0 = raw (amplitude-dependent) only
    ///   1.0 = normalised (amplitude-invariant) only
    /// Applies to all vector features (mfcc, mel, spectral, chroma).
    double n_ratio = 0.0;

    /// Spectral fingerprint comparison range (0-based bin indices).
    int spectral_start = 0;
    int spectral_end = 100;

    // ── Momentum search ────────────────────────────────────────────────

    /// Momentum [0.0, 1.0]: how much the search remembers its previous
    /// direction in fingerprint space.  0.0 = no memory (pure closest),
    /// 1.0 = full inertia (ignores target, follows trajectory).
    double momentum = 0.0;

    /// Momentum decay [0.0, 1.0]: how quickly momentum dissipates per step.
    /// 1.0 = no decay, 0.0 = instant stop.
    double momentum_decay = 0.95;

    // ── Granular post-processing ───────────────────────────────────────

    /// Grain size as a fraction of block size [0.0, 1.0].
    /// 1.0 = full block (no granular effect), 0.01 = tiny grains.
    double grain_size = 1.0;

    /// Grain scatter [0.0, 1.0]: random temporal offset applied to each grain.
    /// 0.0 = grains are sequential, 1.0 = fully scattered.
    double grain_scatter = 0.0;

    /// Grain density [0.1, 4.0]: how many grains overlap per unit time.
    /// 1.0 = normal density, >1.0 = denser/thicker texture.
    double grain_density = 1.0;

    /// Per-grain size variation [0.0, 1.0]: random ± deviation from base grain size.
    /// 0.0 = all grains identical size, 1.0 = size varies ±100%.
    double grain_size_variation = 0.0;

    /// Per-grain amplitude variation [0.0, 1.0]: random gain per grain.
    /// 0.0 = uniform amplitude, 1.0 = ±100% variation.
    double grain_amp_variation = 0.0;

    /// Per-grain pitch jitter [0.0, 1.0]: random playback-speed variation
    /// per grain (resampled via linear interpolation).
    /// 0.0 = no pitch change, 1.0 = ±50% speed variation.
    double grain_pitch_jitter = 0.0;

    /// Per-grain hop randomness [0.0, 1.0]: randomise base hop positions.
    /// 0.0 = fixed grid, 1.0 = fully random placement within the block.
    double grain_hop_randomness = 0.0;

    // ── Spectral morphing ──────────────────────────────────────────────

    /// Spectral morph amount [0.0, 1.0]: cross-fade between consecutive
    /// matched blocks in the frequency domain.
    /// 0.0 = no morphing (hard cuts), 1.0 = full spectral interpolation.
    double spectral_morph = 0.0;

    // ── Stutter / repeat ───────────────────────────────────────────────

    /// Probability that a block triggers a stutter effect [0.0, 1.0].
    /// 0.0 = never stutter, 1.0 = always stutter.
    double stutter_chance = 0.0;

    /// Number of times the stutter sub-region repeats [2, 8].
    int stutter_count = 2;

    // ── Envelope shaping ───────────────────────────────────────────────

    /// Per-block amplitude envelope [0, 4]:
    ///   0 = none (flat)
    ///   1 = exponential decay (punch + fade)
    ///   2 = reverse exponential (swell up)
    ///   3 = tremolo (amplitude modulation)
    ///   4 = pluck (sharp attack, fast decay)
    int envelope_shape = 0;

    /// Envelope intensity [0.0, 1.0]: blends between flat and full envelope.
    /// 0.0 = no effect, 1.0 = full envelope.
    double envelope_amount = 0.0;
};

}  // namespace audio
