#pragma once

#include "../../domain/port/IVideoSource.h"

#include <memory>
#include <optional>
#include <string>

namespace audio::adapter::video {

/**
 * FFmpeg-backed IVideoSource implemented via avcpp (C++ wrapper).
 *
 * An LRU cache (default capacity 4) keeps the most recently accessed
 * FormatContext + VideoDecoderContext open so repeated readFrame() calls
 * do not pay file-open overhead.
 */
class FfmpegVideoSource final : public port::IVideoSource {
public:
    explicit FfmpegVideoSource(std::size_t cache_size = 4, int target_sr = 44100);
    ~FfmpegVideoSource() override;

    FfmpegVideoSource(const FfmpegVideoSource&) = delete;
    FfmpegVideoSource& operator=(const FfmpegVideoSource&) = delete;

    [[nodiscard]] std::unique_ptr<Sound> loadAudio(const std::string& path) override;

    [[nodiscard]] bool getInfo(const std::string& path, int& width, int& height, double& fps,
                               double& duration_seconds) override;

    [[nodiscard]] std::optional<VideoFrame> readFrame(const std::string& path,
                                                      double time_seconds) override;

    [[nodiscard]] std::vector<VideoFrame> readSegment(const std::string& path, double start_seconds,
                                                      double end_seconds) override;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

}  // namespace audio::adapter::video
