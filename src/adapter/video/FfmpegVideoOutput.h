#pragma once

#include "../../domain/port/IVideoOutput.h"
#include "../../domain/port/IVideoSource.h"

#include <memory>
#include <optional>
#include <string>

namespace audio::adapter::video {

/**
 * FFmpeg-backed IVideoOutput for batch mode (implemented via avcpp).
 *
 * For each matched block:
 *   - VideoSegment present: reads the matching video frame via IVideoSource
 *     and encodes it into the output file.
 *   - nullopt (audio-only source): writes a black frame.
 *
 * Writes a video-only H.264/MP4 file. Audio muxing is a separate concern.
 */
class FfmpegVideoOutput final : public port::IVideoOutput {
public:
    FfmpegVideoOutput(std::shared_ptr<port::IVideoSource> source, std::string output_path,
                      int width = 1280, int height = 720, double fps = 25.0);

    ~FfmpegVideoOutput() override;

    FfmpegVideoOutput(const FfmpegVideoOutput&) = delete;
    FfmpegVideoOutput& operator=(const FfmpegVideoOutput&) = delete;

    void onBlock(const std::optional<VideoSegment>& segment, double duration_sec,
                 double block_audio_start_sec = 0.0) override;

    void close() override;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

}  // namespace audio::adapter::video
