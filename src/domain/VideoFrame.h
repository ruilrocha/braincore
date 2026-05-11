#pragma once

#include <cstdint>
#include <vector>

namespace audio {

/**
 * A single decoded video frame.
 *
 * Pixels are stored in RGB24 format (3 bytes per pixel, row-major).
 * Total size: width * height * 3 bytes.
 */
struct VideoFrame {
    int width  = 0;
    int height = 0;

    /// RGB24 pixel data, row-major. Size = width * height * 3.
    std::vector<uint8_t> pixels;

    /// Presentation timestamp within the source file (seconds).
    double timestamp_seconds = 0.0;

    [[nodiscard]] bool empty() const { return pixels.empty(); }

    /// Create a black (zeroed) frame of the given dimensions.
    static VideoFrame black(int w, int h) {
        VideoFrame f;
        f.width  = w;
        f.height = h;
        f.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3, 0);
        return f;
    }
};

} // namespace audio
