#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <thread>

#include <readerwriterqueue/readerwriterqueue.h>

#include "../../domain/VideoSegment.h"
#include "../../domain/port/IVideoDisplay.h"
#include "../../domain/port/IVideoOutput.h"
#include "../../domain/port/IVideoSource.h"

namespace audio::adapter::display {

/**
 * Video output adapter that bridges block-level audio events to a real-time
 * video display using the 3-thread decoupled-clock architecture.
 *
 * Implements IVideoOutput (called from the audio/processing thread) and
 * decodes + renders video frames using IVideoSource + IVideoDisplay.
 *
 * Threads:
 *   Thread A (Audio):   onBlock() → non-blocking SPSC enqueue.
 *   Thread B (Decoder): dequeues BlockCmd → readSegment → display->showFrame().
 *   Thread C (Display): renderLatestFrame() called ~60fps from main thread.
 *
 * Thread C is NOT owned here — the caller (main.cpp) drives it by calling
 * IVideoDisplay::renderLatestFrame() in the main event loop.
 *
 * close() is safe to call from any thread.
 */
class VideoDisplayOutput final : public port::IVideoOutput {
public:
    VideoDisplayOutput(std::shared_ptr<port::IVideoSource> source,
                       std::shared_ptr<port::IVideoDisplay> display);
    ~VideoDisplayOutput() override;

    // Non-copyable / non-movable.
    VideoDisplayOutput(const VideoDisplayOutput&) = delete;
    VideoDisplayOutput& operator=(const VideoDisplayOutput&) = delete;

    // ── IVideoOutput ────────────────────────────────────────────────────

    /// Non-blocking: enqueues the segment into the SPSC queue.
    /// If the queue is full the block is dropped (audio thread never stalls).
    void onBlock(const std::optional<VideoSegment>& segment,
                 double duration_sec) override;

    /// Signal shutdown and join the decoder thread.
    /// Safe to call from any thread.
    void close() override;

private:
    struct BlockCmd {
        std::optional<VideoSegment> segment;
        double duration_sec = 0.0;
    };

    void decoderLoop();

    std::shared_ptr<port::IVideoSource>  source_;
    std::shared_ptr<port::IVideoDisplay> display_;

    moodycamel::ReaderWriterQueue<BlockCmd> block_queue_;

    std::thread       decoder_thread_;
    std::atomic<bool> running_{true};
};

} // namespace audio::adapter::display
