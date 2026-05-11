#pragma once

#include "PixelFormat.h"

#include <cstdint>
#include <variant>
#include <vector>

namespace audio {

/**
 * A single image plane: row-major pixel data + bytes-per-row stride.
 * Stride may be wider than the logical width due to decoder padding.
 */
struct Plane {
    std::vector<uint8_t> data;
    int stride = 0;  ///< Bytes per row.
};

/**
 * YUV 4:2:0 planar (YUV420P).
 *   y : luma,  full resolution  (width  x height)
 *   u : Cb,    half resolution  (width/2 x height/2)
 *   v : Cr,    half resolution  (width/2 x height/2)
 */
struct Yuv420pData {
    Plane y, u, v;

    static Yuv420pData make(int width, int height) {
        const int uv_w = width / 2;
        const int uv_h = height / 2;
        Yuv420pData d;
        d.y.stride = width;
        d.y.data.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        d.u.stride = uv_w;
        d.u.data.resize(static_cast<std::size_t>(uv_w) * static_cast<std::size_t>(uv_h));
        d.v.stride = uv_w;
        d.v.data.resize(static_cast<std::size_t>(uv_w) * static_cast<std::size_t>(uv_h));
        return d;
    }

    /// BT.601 black: Y=16, U=128, V=128.
    static Yuv420pData black(int width, int height) {
        auto d = make(width, height);
        d.y.data.assign(d.y.data.size(), 16);
        d.u.data.assign(d.u.data.size(), 128);
        d.v.data.assign(d.v.data.size(), 128);
        return d;
    }
};

/**
 * Semi-planar YUV 4:2:0 (NV12).
 *   y  : luma,             full resolution (width × height bytes)
 *   uv : interleaved Cb+Cr, half-height    (width × height/2 bytes)
 *
 * Produced directly by VideoToolbox hardware decode on macOS/iOS — using
 * this format avoids the sws_scale CPU conversion to YUV420P.
 * SDL3 renders NV12 natively via SDL_UpdateNVTexture / SDL_PIXELFORMAT_NV12.
 */
struct Nv12Data {
    Plane y;   ///< Luma plane  — width × height bytes.
    Plane uv;  ///< Interleaved chroma plane — width × (height/2) bytes.

    static Nv12Data make(int width, int height) {
        Nv12Data d;
        d.y.stride = width;
        d.y.data.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        d.uv.stride = width;  ///< UV row is full width (interleaved pairs).
        d.uv.data.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height / 2));
        return d;
    }
};

/**
 * Interleaved RGB, 8-bit, 3 bytes per pixel, row-major.
 *   rgb.stride = width * 3 (no padding by default)
 */
struct Rgb24Data {
    Plane rgb;

    static Rgb24Data make(int width, int height) {
        Rgb24Data d;
        d.rgb.stride = width * 3;
        d.rgb.data.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
        return d;
    }
};

// ── VideoFrame ────────────────────────────────────────────────────────────────

/**
 * A single decoded video frame.
 *
 * `pixels` is a std::variant — stored inline inside the struct (no extra heap
 * allocation beyond the pixel plane vectors themselves).  Adding a new format
 * means adding a new data struct and a new variant arm; VideoFrame never changes.
 *
 * Access the active format with std::visit or std::get_if:
 *   if (auto* yuv = std::get_if<Yuv420pData>(&frame.pixels)) { ... }
 */
struct VideoFrame {
    using Pixels = std::variant<Yuv420pData, Rgb24Data, Nv12Data>;

    int width = 0;
    int height = 0;
    Pixels pixels = Yuv420pData::black(0, 0);
    double timestamp_seconds = 0.0;

    [[nodiscard]] bool empty() const {
        return std::visit(
            [](const auto& d) {
                using T = std::decay_t<decltype(d)>;
                if constexpr (std::is_same_v<T, Yuv420pData>)
                    return d.y.data.empty();
                if constexpr (std::is_same_v<T, Rgb24Data>)
                    return d.rgb.data.empty();
                if constexpr (std::is_same_v<T, Nv12Data>)
                    return d.y.data.empty();
            },
            pixels);
    }

    /// Convenience: return the runtime PixelFormat of the active variant arm.
    [[nodiscard]] PixelFormat format() const {
        return std::visit(
            [](const auto& d) -> PixelFormat {
                using T = std::decay_t<decltype(d)>;
                if constexpr (std::is_same_v<T, Yuv420pData>)
                    return PixelFormat::YUV420P;
                if constexpr (std::is_same_v<T, Rgb24Data>)
                    return PixelFormat::RGB24;
                if constexpr (std::is_same_v<T, Nv12Data>)
                    return PixelFormat::NV12;
            },
            pixels);
    }

    static VideoFrame black(int width, int height) {
        VideoFrame f;
        f.width = width;
        f.height = height;
        f.pixels = Yuv420pData::black(width, height);
        return f;
    }
};

}  // namespace audio
