#pragma once

#include "../../domain/port/ISoundFileGateway.h"

namespace audio::adapter::gateway {

/**
 * dr_libs-based implementation of the ISoundFileGateway port.
 *
 * Supports reading WAV, FLAC, and MP3 files. Writing is WAV only.
 * Header-only library — no external linking required (iOS-friendly).
 */
class DrLibsGateway final : public port::ISoundFileGateway {
public:
    [[nodiscard]] std::unique_ptr<Sound> loadSound(
        const std::string& path) override;

    bool saveSound(
        const std::string& path, const Sound& sound) override;
};

} // namespace audio::adapter::gateway
