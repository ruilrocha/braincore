#pragma once

#include "WindowShape.h"
#include "constants.h"

namespace audio {

/**
 * Value object configuring how audio is segmented into blocks.
 */
struct BlockConfig {
    int block_size = kDefaultBlockSize;  ///< Samples per block.

    /**
     * Overlap ratio [0.0, 1.0) for both source segmentation and OLA output synthesis.
     * 0.0 = no overlap (hard cuts); 0.5 = 50% overlap (recommended for smooth crossfade).
     */
    double overlap = 0.0;

    /**
     * Synthesis window shape applied to each block before OLA overlap-add output.
     * NOT used for MFCC analysis — analysis always uses Hann internally (hardcoded in Brain).
     * Hann at 50% overlap gives perfect reconstruction.
     */
    WindowShape window = WindowShape::Rectangle;
};

}  // namespace audio
