#pragma once

#include <optional>

#include "../VideoSegment.h"

namespace audio::port {

/**
 * Port interface for consuming video output during brain-io playback.
 *
 * Called once per matched audio block with the VideoSegment for that block.
 * When the matched block came from an audio-only source, segment is nullopt
 * and the implementation should render a black frame.
 *
 * The CLI adapter (FfmpegVideoOutput) writes frames to a video file.
 * A Swift application would implement this natively using AVFoundation/AVPlayer.
 */
class IVideoOutput {
public:
    virtual ~IVideoOutput() = default;

    /**
     * Called for each output audio block.
     *
     * @param segment        The video segment for the matched block,
     *                       or nullopt if the block has no video source.
     * @param duration_sec   Duration of the audio block in seconds.
     */
    virtual void onBlock(const std::optional<VideoSegment>& segment,
                         double duration_sec) = 0;

    /**
     * Finalise and close the output (flush buffers, write trailers, etc.).
     * Must be called once after all blocks have been processed.
     */
    virtual void close() = 0;
};

} // namespace audio::port
