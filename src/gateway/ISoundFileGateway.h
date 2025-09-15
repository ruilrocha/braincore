#ifndef CPLUSPLUS_ISOUNDFILEGATEWAY_H
#define CPLUSPLUS_ISOUNDFILEGATEWAY_H

#include "../domain/Sound.h"

namespace audio::gateway {
    class ISoundFileGateway {
    public:
        virtual std::unique_ptr<Sound> loadSound(const std::string& path) = 0;
        virtual bool saveSound(const std::string& path, const Sound& sound) = 0;
        virtual ~ISoundFileGateway() = default;
    };
}


#endif //CPLUSPLUS_ISOUNDFILEGATEWAY_H