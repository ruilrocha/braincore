#pragma once

#include "../VideoFrame.h"

namespace audio::port {

/**
 * Port interface for real-time video display.
 *
 * Separates the rendering concern from the block-decoding logic.
 * Implementations render a stream of VideoFrame objects to a visible window.
 *
 * Concrete adapters:
 *   - SdlVideoDisplay  — SDL3 window (desktop, CLI).
 *
 * Threading model:
 *   showFrame()         — safe to call from any thread (decoder thread).
 *   renderLatestFrame() — must be called from the main/UI thread (~60 fps).
 *   close()             — safe to call from any thread; SDL teardown deferred
 *                          to the main thread (inside renderLatestFrame after
 *                          isRunning() returns false).
 */
class IVideoDisplay {
public:
    virtual ~IVideoDisplay() = default;

    /**
     * Store @p frame as the latest frame to display.
     * Thread-safe; called from the decoder thread.
     * Overwrites any previously stored frame (newest always wins).
     */
    virtual void showFrame(VideoFrame frame) = 0;

    /**
     * Upload the latest stored frame to the display and present it.
     * Must be called from the main / UI thread, ~60 fps.
     *
     * After close() has been called, this should destroy any platform
     * resources and return false to signal the caller to exit its loop.
     *
     * @return true  if the display is still running (caller should keep looping).
     *         false if the display has been closed (caller should exit).
     */
    virtual bool renderLatestFrame() = 0;

    /**
     * Signal the display to stop.
     * Thread-safe.  Does NOT destroy platform resources (that must happen on
     * the main thread, inside renderLatestFrame()).
     */
    virtual void close() = 0;

    /// Returns true while the display is open and running.
    [[nodiscard]] virtual bool isRunning() const = 0;
};

}  // namespace audio::port
