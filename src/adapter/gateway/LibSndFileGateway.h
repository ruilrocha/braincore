#pragma once

#include "../../domain/port/ISoundFileGateway.h"

namespace audio::adapter::gateway {

/**
 * libsndfile-based implementation of the ISoundFileGateway port.
 * Reads/writes WAV (extensible to FLAC, OGG, etc.).
 */
class LibSndFileGateway final : public port::ISoundFileGateway {
public:
    [[nodiscard]] std::unique_ptr<Sound> loadSound(
        const std::string& path) override;

    bool saveSound(
        const std::string& path, const Sound& sound) override;
};

} // namespace audio::adapter::gateway

