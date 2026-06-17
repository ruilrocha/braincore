#pragma once

namespace audio {

/**
 * Available block-matching search strategies.
 * Values are stable across versions — safe to store in Swift as Int32.
 */
enum class SearchStrategy : int {  // NOLINT(performance-enum-size) — `: int` stable for Swift
                                   // interop
    Closest = 0,
    VpTree = 1,
    Synaptic = 2,
};

/**
 * Available effect types for the BrainEngine effect pipeline.
 * Values are stable across versions — safe to store in Swift as Int32.
 */
enum class EffectType : int {  // NOLINT(performance-enum-size) — `: int` stable for Swift interop
    SpectralMorph = 0,         ///< Spectral morphing between consecutive blocks.
};

}  // namespace audio
