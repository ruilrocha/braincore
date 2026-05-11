#pragma once

#include "../../domain/VideoFrame.h"
#include "../../domain/VideoSegment.h"
#include "../../domain/port/IVideoDisplay.h"
#include "../../domain/port/IVideoOutput.h"
#include "../../domain/port/IVideoSource.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <readerwriterqueue/readerwriterqueue.h>
#include <thread>

namespace audio::adapter::display {

/**
 * Real-time video output adapter using an audio-clock pull model.
 *
 * ## Architecture
 *
 * Three threads cooperate:
 *
 *   Thread A (audio/processing)
 *     → calls onBlock() — non-blocking SPSC enqueue of BlockCmd.
 *
 *   Thread B (decoder, owned here)
 *     → dequeues BlockCmd, calls IVideoSource::readSegment(), annotates each
 *       frame with its absolute audio presentation time, and pushes it into
 *       the shared timed_frames_ buffer.
 *
 *   Thread C (main / SDL, ~60 Hz)
 *     → calls renderFrameForTime(audio_time), which atomically selects the
 *       frame whose window contains audio_time and pushes it to IVideoDisplay.
 *     → then calls IVideoDisplay::renderLatestFrame() to blit to screen.
 *
 * ## Sync model
 *
 * Each frame carries [audio_start, audio_end) in seconds of "true" playback
 * time (as reported by IAudioOutput::getAudioTimeSec()).  renderFrameForTime()
 * drops expired frames, finds the current one, and repeats the last seen frame
 * when no current frame is available (decoder catching up, or gap between
 * blocks).  This matches how VLC / FFplay implement A/V sync.
 */
class VideoDisplayOutput final : public port::IVideoOutput {
public:
    VideoDisplayOutput(std::shared_ptr<port::IVideoSource> source,
                       std::shared_ptr<port::IVideoDisplay> display);
    ~VideoDisplayOutput() override;

    VideoDisplayOutput(const VideoDisplayOutput&) = delete;
    VideoDisplayOutput& operator=(const VideoDisplayOutput&) = delete;

    // ── IVideoOutput ────────────────────────────────────────────────────

    /// Non-blocking: enqueues the segment for background decoding.
    void onBlock(const std::optional<VideoSegment>& segment, double duration_sec,
                 double block_audio_start_sec = 0.0) override;

    /**
     * Called from the main render loop (~60 Hz).
     *
     * Selects the video frame whose audio window contains @p audio_time_sec
     * and pushes it to the display via IVideoDisplay::showFrame().
     * Repeats the last frame when the decoder hasn't caught up yet or there
     * is a gap between blocks.
     */
    void renderFrameForTime(double audio_time_sec) override;

    void close() override;

private:
    struct BlockCmd {
        std::optional<VideoSegment> segment;
        double duration_sec = 0.0;
        double block_audio_start_sec = 0.0;
    };

    /// A decoded video frame annotated with its audio presentation window.
    struct TimedFrame {
        double audio_start = 0.0;  ///< Seconds: start of presentation window.
        double audio_end = 0.0;    ///< Seconds: end   of presentation window.
        VideoFrame frame;
    };

    void decoderLoop();

    std::shared_ptr<port::IVideoSource> source_;
    std::shared_ptr<port::IVideoDisplay> display_;

    /// SPSC queue from Thread A → Thread B.
    moodycamel::ReaderWriterQueue<BlockCmd> block_queue_;

    /// Pre-decoded frames sorted by audio_start (Thread B writes, Thread C reads).
    std::deque<TimedFrame> timed_frames_;
    std::mutex frames_mutex_;

    /// Last frame successfully shown — repeated when no current frame is available.
    std::optional<VideoFrame> last_shown_frame_;

    // Maximum number of decoded frames to keep buffered ahead.
    static constexpr std::size_t kMaxBufferedFrames = 512;

    std::thread decoder_thread_;
    std::atomic<bool> running_{true};
};

}  // namespace audio::adapter::display
