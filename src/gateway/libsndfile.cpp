#include "libsndfile.h"

#include <algorithm>

#include "sndfile.h"
#include <iostream>

namespace audio::gateway {
    std::unique_ptr<Sound> libsndfile::loadSound(const std::string &path) {
        SF_INFO sfinfo;
        SNDFILE* file = sf_open(path.c_str(), SFM_READ, &sfinfo);
        if (!file) {
            return nullptr;
        }

        // Extracting
        std::vector<float> interleaved(sfinfo.frames * sfinfo.channels);
        sf_readf_float(file, interleaved.data(), sfinfo.frames);
        sf_close(file);

        // Split interleaved data into channels
        std::vector channels(sfinfo.channels, std::vector<float>(sfinfo.frames));
        for (int i = 0; i < sfinfo.frames; ++i) {
            for (int ch = 0; ch < sfinfo.channels; ++ch) {
                channels[ch][i] = interleaved[i * sfinfo.channels + ch];
            }
        }

        return std::make_unique<Sound>(std::move(channels), sfinfo.frames, sfinfo.channels, sfinfo.samplerate);
    }

    bool libsndfile::saveSound(const std::string &path, const Sound& sound) {
        SF_INFO sfinfo;
        sfinfo.frames = sound.getNumSamples();
        sfinfo.channels = sound.getNumChannels();
        sfinfo.samplerate = sound.getSampleRate();
        sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16; // Change format as needed

        if (SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &sfinfo)) {
            const auto& channels = sound.getChannels();
            const int frames = sound.getNumSamples();
            const int channelsCount = sound.getNumChannels();

            if (std::ranges::any_of(channels, [](const auto& ch) { return ch.empty(); })) {
                std::cout << "Error: One or more channels are empty." << std::endl;
                sf_close(file);
                return false;
            }
            // Interleave
            std::vector<float> interleaved(frames * channelsCount);
            for (int i = 0; i < frames; ++i) {
                for (int ch = 0; ch < channelsCount; ++ch) {
                    interleaved[i * channelsCount + ch] = channels[ch][i];
                }
            }
            sf_writef_float(file, interleaved.data(), frames);
            sf_close(file);
        } else {
            std::cout << "Error creating file " << path << std::endl;
            return false;
        }
        return true;
    }


} // gateway
// audio