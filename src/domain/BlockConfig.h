#pragma once

#include "WindowShape.h"
#include "constants.h"

namespace audio {

/**
 * Value object configuring how audio is segmented into blocks.
 */
struct BlockConfig {
    int block_size = kDefaultBlockSize;           ///< Samples per block.
    double overlap = 0.0;                         ///< Overlap ratio [0.0, 1.0): 0 = no overlap, 0.5 = 50% overlap.
    WindowShape window = WindowShape::Rectangle;  ///< Window shape applied before analysis.
};

}  // namespace audio
