//
// Created by vscode on 9/8/25.
//

#ifndef CPLUSPLUS_ISOUNDFILEGATEWAY_H
#define CPLUSPLUS_ISOUNDFILEGATEWAY_H

#include "../Sound.h"

namespace audio::gateway {
    class ISoundFileGateway {
    public:
        virtual std::unique_ptr<Sound> loadSound(const std::string& path) = 0;
        virtual void saveSound(const std::string& path, const Sound& sound) = 0;
        virtual ~ISoundFileGateway() = default;
    };
}


#endif //CPLUSPLUS_ISOUNDFILEGATEWAY_H