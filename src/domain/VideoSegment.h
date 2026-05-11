#pragma once

#include <string>

namespace audio {

/**
 * Identifies a time-bounded segment of a video source file.
 *
 * Stored on Block when the block was sourced from a video file.
 * Absent (std::nullopt) when the block came from an audio-only source,
 * in which case the video output should render a black frame.
 */
struct VideoSegment {
    /// Absolute path to the source video file.
    std::string source_path;

    /// Time in seconds where this block starts in the source video.
    double offset_seconds = 0.0;

    /// Duration of this block in seconds.
    double duration_seconds = 0.0;
};

/**
 * Metadata passed to Brain::addSound() to associate video with a sound.
 * The audio track must be pre-extracted (via IVideoSource::loadAudio).
 */
struct VideoMetadata {
    /// Path to the original video file (used for frame extraction at playback).
    std::string path;

    /// Time offset in seconds where this sound starts in the video.
    /// Typically 0.0 for full-file loads.
    double start_offset_seconds = 0.0;
};

} // namespace audio
