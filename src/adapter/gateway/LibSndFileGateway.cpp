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

    std::vector<double> interleaved(
        static_cast<std::size_t>(sfinfo.frames) * static_cast<std::size_t>(sfinfo.channels));
    sf_readf_double(file, interleaved.data(), sfinfo.frames);
    sf_close(file);

    // De-interleave into per-channel vectors.
    const auto num_ch = static_cast<std::size_t>(sfinfo.channels);
    const auto num_frames = static_cast<std::size_t>(sfinfo.frames);
    std::vector<Channel> channels(num_ch, Channel(num_frames));
    for (std::size_t i = 0; i < num_frames; ++i) {
        for (std::size_t ch = 0; ch < num_ch; ++ch) {
            channels[ch][i] = interleaved[i * num_ch + ch];
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
    const auto frames = static_cast<std::size_t>(sound.getNumSamples());
    const auto num_ch = static_cast<std::size_t>(sound.getNumChannels());
    Channel interleaved(frames * num_ch);
    for (std::size_t i = 0; i < frames; ++i) {
        for (std::size_t ch = 0; ch < num_ch; ++ch) {
            interleaved[i * num_ch + ch] = channels[ch][i];
        }
    }

    sf_writef_double(file, interleaved.data(), static_cast<sf_count_t>(frames));
    sf_close(file);
    return true;
}

} // namespace audio::adapter::gateway

