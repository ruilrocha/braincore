#pragma once

#include "WindowShape.h"
#include "constants.h"

namespace audio {

/**
 * Value object configuring how audio is segmented into blocks.
 */
struct BlockConfig {
    int block_size = kDefaultBlockSize;           ///< Samples per block.
    int overlap = 0;                              ///< Overlap between consecutive blocks.
    WindowShape window = WindowShape::Rectangle;  ///< Window shape applied before analysis.
};

}  // namespace audio
