#pragma once

#include <cstdint>

namespace audio {

/**
 * Pixel format of a VideoFrame.
 *
 * Expressed as a domain concept — adapters are responsible for mapping this
 * to their library-specific format constants (e.g. SDL_PIXELFORMAT_IYUV,
 * AV_PIX_FMT_YUV420P).
 */
enum class PixelFormat : std::uint8_t {
    YUV420P,  ///< Planar YUV 4:2:0 — Y full-res, U/V half-res each axis.
    RGB24,    ///< Interleaved RGB, 8-bit per channel, 3 bytes per pixel.
};

}  // namespace audio
