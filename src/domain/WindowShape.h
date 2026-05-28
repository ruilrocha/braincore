#pragma once

namespace audio {

/** Window function applied to each block before fingerprinting. */
enum class WindowShape : int {
    Rectangle = 0,
    Hamming = 1,
    Hann = 2,
    Blackman = 3,
    Bartlett = 4,
    FlatTop = 5,
    Gaussian = 6,
};

}  // namespace audio
