#pragma once

#include "../Sound.h"

#include <memory>
#include <string>

namespace audio::port {

/**
 * Port: loading and saving audio files.
 *
 * Lives in the domain layer — concrete implementations (DrLibsGateway)
 * are adapters in src/adapter/gateway/.
 */
class ISoundFileGateway {
public:
    virtual ~ISoundFileGateway() = default;

    [[nodiscard]] virtual std::unique_ptr<Sound> loadSound(const std::string& path) = 0;

    virtual bool saveSound(const std::string& path, const Sound& sound) = 0;
};

}  // namespace audio::port
