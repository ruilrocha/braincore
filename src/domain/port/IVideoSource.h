#pragma once

#include "../Sound.h"
#include "../VideoFrame.h"
#include "../VideoSegment.h"

#include <memory>
#include <optional>
#include <string>

namespace audio::port {

/**
 * Port interface for reading from video files.
 *
 * Responsibilities:
 *   - Extract the audio track for brain ingestion.
 *   - Decode individual video frames by timestamp for playback.
 *   - Report video file metadata (dimensions, fps, duration).
 *
 * The CLI adapter uses FFmpeg (libavformat + libavcodec + libswscale).
 */
class IVideoSource {
public:
    virtual ~IVideoSource() = default;

    /**
     * Extract the audio track from @p path and return it as a Sound.
     *
     * @param path  Path to the video file.
     * @return Loaded audio, or nullptr on failure.
     */
    [[nodiscard]] virtual std::unique_ptr<Sound> loadAudio(const std::string& path) = 0;

    /**
     * Query video metadata without fully decoding the file.
     *
     * @param path              Path to the video file.
     * @param width             Output: frame width in pixels.
     * @param height            Output: frame height in pixels.
     * @param fps               Output: frames per second.
     * @param duration_seconds  Output: total duration.
     * @return true on success, false if the file cannot be opened.
     */
    [[nodiscard]] virtual bool getInfo(const std::string& path, int& width, int& height,
                                       double& fps, double& duration_seconds) = 0;

    /**
     * Decode video frames whose PTS falls in [start_seconds, end_seconds).
     *
     * The implementation may seek to @p start_seconds if not already positioned
     * there, but avoids seeking when the context is already positioned within a
     * short forward window (sequential reads will not re-seek).
     *
     * @param path           Path to the video file.
     * @param start_seconds  Start of the time window (inclusive).
     * @param end_seconds    End of the time window (exclusive).
     * @return Vector of decoded frames (may be empty on EOF/error).
     */
    [[nodiscard]] virtual std::vector<VideoFrame> readSegment(const std::string& path,
                                                              double start_seconds,
                                                              double end_seconds) = 0;
};

}  // namespace audio::port
