//
// Created by vscode on 9/8/25.
//

#pragma once

#include <memory>
#include "../Sound.h"
#include "ISoundFileGateway.h"

namespace audio::gateway {
    class libsndfile: public ISoundFileGateway {
    public:
        libsndfile() = default;
        std::unique_ptr<Sound> loadSound(const std::string& path) override;
        void saveSound(const std::string& path, const Sound& sound) override;
    };
} // gateway
// audio