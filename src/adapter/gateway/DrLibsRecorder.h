#pragma once

#include "../../domain/port/IRecorder.h"

#include <string>
#include <vector>

namespace audio::adapter::gateway {

/**
 * Audio recorder using dr_wav.
 *
 * Writes audio incrementally to a WAV file (32-bit float) using
 * dr_wav's sequential write API. Header-only, no external deps.
 */
class DrLibsRecorder final : public port::IRecorder {
public:
    DrLibsRecorder() = default;
    ~DrLibsRecorder() override;

    DrLibsRecorder(const DrLibsRecorder&) = delete;
    DrLibsRecorder& operator=(const DrLibsRecorder&) = delete;

    bool open(const std::string& path, int sample_rate, int channels) override;
    void write(const std::vector<double>& samples) override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;

private:
    struct Impl;
    Impl* impl_ = nullptr;
    int channels_ = 0;
};

}  // namespace audio::adapter::gateway
