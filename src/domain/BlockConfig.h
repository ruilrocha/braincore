#pragma once

#include "constants.h"

#include <cstdint>

namespace audio {

/**
 * Window shapes for audio block analysis.
 * Applied to samples before fingerprinting to reduce spectral leakage.
 */
enum class WindowShape : std::uint8_t {
    Rectangle,  ///< No windowing (flat).
    Hamming,    ///< Good general-purpose, reduced side lobes.
    Hann,       ///< Similar to Hamming, zero at endpoints.
    Blackman,   ///< Narrower main lobe, better side-lobe suppression.
    Bartlett,   ///< Triangular window.
    FlatTop,    ///< Very flat passband — good for amplitude measurement.
    Gaussian,   ///< Gaussian-shaped with sigma = 0.5.
};

/**
 * Value object configuring how audio is segmented into blocks.
 *
 * Source (brain) and target can each have their own BlockConfig, allowing
 * different block sizes, overlaps, and window shapes for ingestion vs.
 * reconstruction.
 */
struct BlockConfig {
    int block_size = kDefaultBlockSize;           ///< Samples per block.
    int overlap = 0;                              ///< Overlap between consecutive blocks (samples).
    WindowShape window = WindowShape::Rectangle;  ///< Window shape applied before analysis.
};

}  // namespace audio
