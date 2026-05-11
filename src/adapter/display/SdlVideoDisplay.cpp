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

    // Preserve source aspect ratio (letterbox / pillarbox) on window resize.
    SDL_SetRenderLogicalPresentation(renderer_, width_, height_,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
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
        // Recreate texture if frame dimensions changed.
        if (frame->width != width_ || frame->height != height_) {
            SDL_DestroyTexture(texture_);
            width_ = frame->width;
            height_ = frame->height;
            SDL_SetRenderLogicalPresentation(renderer_, width_, height_,
                                             SDL_LOGICAL_PRESENTATION_LETTERBOX);
            texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24,
                                         SDL_TEXTUREACCESS_STREAMING, width_, height_);
            if (texture_ == nullptr) {
                return false;
            }
        }

        SDL_UpdateTexture(texture_, nullptr, frame->pixels.data(),
                          frame->width * 3);  // pitch = width * 3 (RGB24)
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
