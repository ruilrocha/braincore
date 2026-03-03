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
 *   - "novelty"     = usage_weight   — penalises re-used blocks
 *   - "boredom"     = usage_falloff  — how fast blocks become available again
 *   - "blend_ratio" — blend between primary and secondary fingerprint comparison
 *   - "n_ratio"     — blend between raw and normalised fingerprint comparison
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

    // ── Comparison blend ───────────────────────────────────────────────

    /// Primary-vs-secondary fingerprint blend ratio [0.0, 1.0]:
    ///   0.0 = use only secondary fingerprint
    ///   1.0 = use only primary fingerprint
    double blend_ratio = 1.0;

    /// Raw-vs-normalised comparison ratio [0.0, 1.0]:
    ///   0.0 = raw (amplitude-dependent) only
    ///   1.0 = normalised (amplitude-invariant) only
    double n_ratio = 0.0;

    /// Secondary fingerprint comparison range (0-based).
    int secondary_start = 0;
    int secondary_end   = 100;

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

} // namespace audio
