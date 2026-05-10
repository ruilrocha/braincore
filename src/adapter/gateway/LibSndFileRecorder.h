#pragma once

#include <string>
#include <vector>

#include "../../domain/port/IRecorder.h"

namespace audio::adapter::gateway {

/**
 * Audio recorder using libsndfile.
 *
 * Writes audio incrementally to a WAV file (PCM 24-bit) via
 * sf_writef_double, allowing arbitrarily long recordings without
 * accumulating audio data in memory.
 */
class LibSndFileRecorder final : public port::IRecorder {
public:
    LibSndFileRecorder() = default;
    ~LibSndFileRecorder() override;

    // Non-copyable, non-movable (owns file handle).
    LibSndFileRecorder(const LibSndFileRecorder&) = delete;
    LibSndFileRecorder& operator=(const LibSndFileRecorder&) = delete;

    bool open(const std::string& path, int sample_rate, int channels) override;
    void write(const std::vector<double>& samples) override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;

private:
    struct Impl;
    Impl* impl_ = nullptr;
    int channels_ = 0;
};

} // namespace audio::adapter::gateway

