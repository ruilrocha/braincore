#pragma once

#include <cstdint>

namespace audio {

/** Window function applied to each block before fingerprinting. */
enum class WindowShape : std::uint8_t {
    Rectangle = 0,
    Hamming = 1,
    Hann = 2,
    Blackman = 3,
    Bartlett = 4,
    FlatTop = 5,
    Gaussian = 6,
};

}  // namespace audio
