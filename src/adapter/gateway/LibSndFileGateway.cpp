#include "LibSndFileGateway.h"

#include <algorithm>
#include <iostream>
#include <ranges>

#include "sndfile.h"

namespace audio::adapter::gateway {

std::unique_ptr<Sound> LibSndFileGateway::loadSound(const std::string& path) {
    SF_INFO sfinfo{};
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &sfinfo);
    if (!file) {
        return nullptr;
    }

    std::vector<double> interleaved(sfinfo.frames * sfinfo.channels);
    sf_readf_double(file, interleaved.data(), sfinfo.frames);
    sf_close(file);

    // De-interleave into per-channel vectors.
    std::vector<Channel> channels(sfinfo.channels, Channel(sfinfo.frames));
    for (sf_count_t i = 0; i < sfinfo.frames; ++i) {
        for (int ch = 0; ch < sfinfo.channels; ++ch) {
            channels[ch][i] = interleaved[i * sfinfo.channels + ch];
        }
    }

    return std::make_unique<Sound>(std::move(channels), sfinfo.samplerate);
}

bool LibSndFileGateway::saveSound(const std::string& path,
                                   const Sound& sound) {
    SF_INFO sfinfo{};
    sfinfo.frames     = sound.getNumSamples();
    sfinfo.channels   = sound.getNumChannels();
    sfinfo.samplerate = sound.getSampleRate();
    sfinfo.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &sfinfo);
    if (!file) {
        std::cerr << "Error creating file " << path << '\n';
        return false;
    }

    const auto& channels = sound.getChannels();
    if (std::ranges::any_of(channels, [](const auto& ch) { return ch.empty(); })) {
        std::cerr << "Error: one or more channels are empty.\n";
        sf_close(file);
        return false;
    }

    // Interleave.
    const int frames = sound.getNumSamples();
    const int num_ch = sound.getNumChannels();
    Channel interleaved(frames * num_ch);
    for (int i = 0; i < frames; ++i) {
        for (int ch = 0; ch < num_ch; ++ch) {
            interleaved[i * num_ch + ch] = channels[ch][i];
        }
    }

    sf_writef_double(file, interleaved.data(), frames);
    sf_close(file);
    return true;
}

} // namespace audio::adapter::gateway

