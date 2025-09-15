#ifndef CPLUSPLUS_SOUND_H
#define CPLUSPLUS_SOUND_H

#include <utility>
#include <vector>
namespace audio {
    class Sound {
        std::vector<std::vector<float>> channels;
        int num_samples;
        int num_channels;
        int sample_rate;
    public:
        Sound(std::vector<std::vector<float>> channels, const int samples, const int num_channels, const int sample_rate)
        : channels(std::move(channels)), num_samples(samples), num_channels(num_channels), sample_rate(sample_rate) {}

        [[nodiscard]] const std::vector<std::vector<float>> &getChannels() const;
        [[nodiscard]] int getNumSamples() const;
        [[nodiscard]] int getNumChannels() const;
        [[nodiscard]] int getSampleRate() const;

    };
} // audio

#endif //CPLUSPLUS_SOUND_H