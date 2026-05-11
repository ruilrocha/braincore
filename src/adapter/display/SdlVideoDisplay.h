#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "../../domain/VideoFrame.h"
#include "../../domain/port/IVideoDisplay.h"

// Forward-declare SDL types to keep the header SDL-free.
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace audio::adapter::display {

/**
 * SDL3 video display — implements IVideoDisplay (rendering only).
 *
 * Responsibilities:
 *   - Open an SDL3 window and create a streaming RGB24 texture.
 *   - showFrame(): store the latest decoded frame (thread-safe, any thread).
 *   - renderLatestFrame(): upload texture + present (call ~60fps from main thread).
 *   - close(): signal stop (thread-safe); SDL teardown deferred to main thread.
 *
 * Threading:
 *   - showFrame()         — called from decoder thread (VideoDisplayOutput)
 *   - renderLatestFrame() — MUST be called from the main / UI thread (macOS requirement)
 *   - close()             — thread-safe signal; returns after setting running_ = false
 *
 * Usage:
 *   auto display = std::make_shared<SdlVideoDisplay>(width, height);
 *   auto out     = std::make_shared<VideoDisplayOutput>(video_src, display);
 *   // main loop:
 *   while (display->renderLatestFrame()) {
 *       // poll SDL events, handle commands, sleep 16ms
 *   }
 */
class SdlVideoDisplay final : public port::IVideoDisplay {
public:
    SdlVideoDisplay(int width, int height);
    ~SdlVideoDisplay() override;

    // Non-copyable / non-movable.
    SdlVideoDisplay(const SdlVideoDisplay&) = delete;
    SdlVideoDisplay& operator=(const SdlVideoDisplay&) = delete;

    // ── IVideoDisplay ────────────────────────────────────────────────────
    void showFrame(VideoFrame frame) override;
    bool renderLatestFrame() override;
    void close() override;
    [[nodiscard]] bool isRunning() const override { return running_.load(); }

private:
    /// Destroy SDL resources — MUST be called from the main thread.
    void destroySdl();

    int width_;
    int height_;

    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  texture_  = nullptr;

    mutable std::mutex          frame_mutex_;
    std::shared_ptr<VideoFrame> latest_frame_;

    std::atomic<bool> running_{true};
    std::atomic<bool> sdl_destroyed_{false};
};

} // namespace audio::adapter::display
