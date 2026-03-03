#pragma once

namespace audio {

/// Factor by which a block's usage counter is incremented when selected.
/// Centralised here so all search strategies share the same constant.
constexpr double kUsageFactor = 1000.0;

/**
 * Value object holding all parameters that control how the brain searches
 * for matching blocks.
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

    /// "Boredom": usage falloff rate [0.0, 1.0].  Each tick every block's
    /// usage counter is multiplied by this.  Lower values deplete blocks
    /// faster, promoting variety.  1.0 = no depletion.
    double usage_falloff = 1.0;

    /// "Novelty": weight applied to usage when scoring candidates [0.0, 1.0].
    /// Higher values penalise re-used blocks more strongly.
    double usage_weight = 0.0;

    // ── Comparison blend ───────────────────────────────────────────────

    /// Primary-vs-secondary fingerprint blend ratio [0.0, 1.0]:
    ///   0.0 = use only secondary fingerprint
    ///   1.0 = use only primary fingerprint
    ///   in-between = weighted blend of both
    double blend_ratio = 1.0;

    /// Raw-vs-normalised comparison ratio [0.0, 1.0]:
    ///   0.0 = raw (amplitude-dependent) fingerprints only
    ///   1.0 = normalised (amplitude-invariant) fingerprints only
    ///   in-between = weighted blend
    double n_ratio = 0.0;

    /// Secondary fingerprint comparison range (0-based, inclusive).
    /// Allows focusing the secondary comparison on a specific bin range.
    int secondary_start = 0;
    int secondary_end   = 100;
};

} // namespace audio
