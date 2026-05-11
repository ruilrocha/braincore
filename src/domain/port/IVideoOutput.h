#pragma once

#include "../VideoSegment.h"

#include <optional>

namespace audio::port {

/**
 * Port interface for consuming video output during brain-io playback.
 *
 * Called once per matched audio block with the VideoSegment for that block.
 * When the matched block came from an audio-only source, segment is nullopt
 * and the implementation should render a black frame.
 *
 * The CLI adapter (FfmpegVideoOutput) writes frames to a video file.
 * The display adapter (VideoDisplayOutput + SdlVideoDisplay) renders frames in real-time.
 */
class IVideoOutput {
public:
    virtual ~IVideoOutput() = default;

    /**
     * Called for each output audio block.
     *
     * @param segment              The video segment for the matched block,
     *                             or nullopt if the block has no video source.
     * @param duration_sec         Duration of the audio block in seconds.
     * @param block_audio_start_sec  Absolute playback time (in seconds) at which
     *                             the first sample of this block will be heard.
     *                             Computed as total_interleaved_samples_written_before_block
     *                             / (sample_rate * channels).  Pass 0.0 if unavailable.
     */
    virtual void onBlock(const std::optional<VideoSegment>& segment, double duration_sec,
                         double block_audio_start_sec = 0.0) = 0;

    /**
     * Called from the main render loop (~60 Hz) to display the video frame that
     * corresponds to the current audio playback position.
     *
     * Implementations that maintain a time-indexed frame buffer should pick the
     * frame at @p audio_time_sec and push it to the display.  The default
     * no-op implementation is suitable for batch/offline outputs.
     *
     * @param audio_time_sec  Current "true" audio playback position in seconds,
     *                        as returned by IAudioOutput::getAudioTimeSec().
     */
    virtual void renderFrameForTime(double /*audio_time_sec*/) {}

    /**
     * Finalise and close the output (flush buffers, write trailers, etc.).
     * Must be called once after all blocks have been processed.
     */
    virtual void close() = 0;
};

}  // namespace audio::port
