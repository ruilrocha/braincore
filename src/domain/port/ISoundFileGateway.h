#pragma once

#include <memory>
#include <string>
#include "../Sound.h"

namespace audio::port {

/**
 * Port: loading and saving audio files.
 *
 * Lives in the domain layer — concrete implementations (LibSndFileGateway)
 * are adapters in src/adapter/gateway/.
 */
class ISoundFileGateway {
public:
    virtual ~ISoundFileGateway() = default;

    [[nodiscard]] virtual std::unique_ptr<Sound> loadSound(
        const std::string& path) = 0;

    virtual bool saveSound(
        const std::string& path, const Sound& sound) = 0;
};

} // namespace audio::port
