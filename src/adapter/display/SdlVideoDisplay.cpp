#include "SdlVideoDisplay.h"

#include <SDL3/SDL.h>
#include <format>
#include <iostream>

namespace audio::adapter::display {

// ── Constructor ─────────────────────────────────────────────────────────────

SdlVideoDisplay::SdlVideoDisplay(int width, int height) : width_(width), height_(height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << std::format("SDL_Init error: {}\n", SDL_GetError());
        running_ = false;
        return;
    }

    window_ = SDL_CreateWindow("brain-io video", width_, height_, SDL_WINDOW_RESIZABLE);
    if (window_ == nullptr) {
        std::cerr << std::format("SDL_CreateWindow error: {}\n", SDL_GetError());
        running_ = false;
        SDL_Quit();
        return;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (renderer_ == nullptr) {
        std::cerr << std::format("SDL_CreateRenderer error: {}\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        running_ = false;
        SDL_Quit();
        return;
    }

    // VSync: let the display driver pace SDL_RenderPresent to the screen
    // refresh rate so frame timing is smooth and GPU usage is low.
    SDL_SetRenderVSync(renderer_, 1);

    // Preserve source aspect ratio (letterbox / pillarbox) on window resize.
    SDL_SetRenderLogicalPresentation(renderer_, width_, height_,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    texture_format_ = static_cast<int>(SDL_PIXELFORMAT_IYUV);
    texture_ = SDL_CreateTexture(renderer_, static_cast<SDL_PixelFormat>(texture_format_), SDL_TEXTUREACCESS_STREAMING,
                                 width_, height_);
    if (texture_ == nullptr) {
        std::cerr << std::format("SDL_CreateTexture error: {}\n", SDL_GetError());
        SDL_DestroyRenderer(renderer_);
        SDL_DestroyWindow(window_);
        renderer_ = nullptr;
        window_ = nullptr;
        running_ = false;
        SDL_Quit();
    }
}

// ── Destructor ───────────────────────────────────────────────────────────────

SdlVideoDisplay::~SdlVideoDisplay() {
    running_ = false;
    destroySdl();
}

// ── IVideoDisplay::showFrame (any thread) ───────────────────────────────────

void SdlVideoDisplay::showFrame(VideoFrame frame) {
    auto ptr = std::make_shared<VideoFrame>(std::move(frame));
    std::scoped_lock lock(frame_mutex_);
    latest_frame_ = std::move(ptr);
}

// ── IVideoDisplay::renderLatestFrame (main thread, ~60fps) ──────────────────

bool SdlVideoDisplay::renderLatestFrame() {
    if (!running_) {
        destroySdl();
        return false;
    }

    if (renderer_ == nullptr || texture_ == nullptr) {
        return false;
    }

    // Grab latest frame (short critical section).
    std::shared_ptr<VideoFrame> frame;
    {
        std::scoped_lock lock(frame_mutex_);
        frame = latest_frame_;
    }

    if (frame && !frame->empty()) {
        // Map the active pixel format to an SDL format constant.
        const uint32_t sdl_fmt = std::visit(
            [](const auto& d) -> int {
                using T = std::decay_t<decltype(d)>;
                if constexpr (std::is_same_v<T, Yuv420pData>) return static_cast<int>(SDL_PIXELFORMAT_IYUV);
                if constexpr (std::is_same_v<T, Rgb24Data>)   return static_cast<int>(SDL_PIXELFORMAT_RGB24);
            },
            frame->pixels);

        // Recreate texture if dimensions or pixel format changed.
        if (frame->width != width_ || frame->height != height_ || sdl_fmt != texture_format_) {
            SDL_DestroyTexture(texture_);
            width_ = frame->width;
            height_ = frame->height;
            texture_format_ = sdl_fmt;
            SDL_SetRenderLogicalPresentation(renderer_, width_, height_,
                                             SDL_LOGICAL_PRESENTATION_LETTERBOX);
            texture_ = SDL_CreateTexture(renderer_, static_cast<SDL_PixelFormat>(texture_format_),
                                         SDL_TEXTUREACCESS_STREAMING, width_, height_);
            if (texture_ == nullptr) {
                return false;
            }
        }

        if (const auto* yuv = std::get_if<Yuv420pData>(&frame->pixels)) {
            SDL_UpdateYUVTexture(texture_, nullptr, yuv->y.data.data(), yuv->y.stride,
                                 yuv->u.data.data(), yuv->u.stride, yuv->v.data.data(),
                                 yuv->v.stride);
        } else if (const auto* rgb = std::get_if<Rgb24Data>(&frame->pixels)) {
            SDL_UpdateTexture(texture_, nullptr, rgb->rgb.data.data(), rgb->rgb.stride);
        }
        SDL_RenderClear(renderer_);
        SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
    } else {
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_RenderPresent(renderer_);
    }

    return true;
}

// ── IVideoDisplay::close (any thread) ───────────────────────────────────────

void SdlVideoDisplay::close() {
    running_ = false;
    // SDL teardown deferred to renderLatestFrame() on the main thread.
}

// ── destroySdl (main thread only) ────────────────────────────────────────────

void SdlVideoDisplay::destroySdl() {
    if (sdl_destroyed_.exchange(true)) {
        return;  // guard against double-destroy
    }

    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    SDL_Quit();
}

}  // namespace audio::adapter::display
