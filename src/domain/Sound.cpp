#include "Sound.h"

namespace audio {
    int Sound::getNumSamples() const {
        return num_samples;
    }

    int Sound::getNumChannels() const {
        return num_channels;
    }

    const std::vector<std::vector<float>> &Sound::getChannels() const {
        return channels;
    }

    int Sound::getSampleRate() const {
        return sample_rate;
    }

} // audio