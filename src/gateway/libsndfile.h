#pragma once

#include <memory>
#include "../domain/Sound.h"
#include "ISoundFileGateway.h"

namespace audio::gateway {
    class libsndfile: public ISoundFileGateway {
    public:
        libsndfile() = default;
        std::unique_ptr<Sound> loadSound(const std::string& path) override;
        bool saveSound(const std::string& path, const Sound& sound) override;
    };
} // gateway
// audio