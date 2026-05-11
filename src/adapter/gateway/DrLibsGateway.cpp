#include "DrLibsGateway.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

// dr_libs implementation — define in exactly one translation unit.
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>

#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>

#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>

namespace audio::adapter::gateway {

namespace {

enum class AudioFormat { Wav, Flac, Mp3, Unknown };

AudioFormat detectFormat(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    std::ranges::transform(ext, ext.begin(), ::tolower);

    if (ext == ".wav" || ext == ".wave") return AudioFormat::Wav;
    if (ext == ".flac") return AudioFormat::Flac;
    if (ext == ".mp3") return AudioFormat::Mp3;
    return AudioFormat::Unknown;
}

} // namespace

std::unique_ptr<Sound> DrLibsGateway::loadSound(const std::string& path) {
    const auto format = detectFormat(path);

    unsigned int channels = 0;
    unsigned int sample_rate = 0;
    drwav_uint64 total_frames = 0;
    float* float_data = nullptr;

    switch (format) {
        case AudioFormat::Wav: {
            float_data = drwav_open_file_and_read_pcm_frames_f32(
                path.c_str(), &channels, &sample_rate, &total_frames, nullptr);
            break;
        }
        case AudioFormat::Flac: {
            float_data = drflac_open_file_and_read_pcm_frames_f32(
                path.c_str(), &channels, &sample_rate, &total_frames, nullptr);
            break;
        }
        case AudioFormat::Mp3: {
            drmp3_config config{};
            float_data = drmp3_open_file_and_read_pcm_frames_f32(
                path.c_str(), &config, &total_frames, nullptr);
            if (float_data) {
                channels = config.channels;
                sample_rate = config.sampleRate;
            }
            break;
        }
        default: {
            std::cerr << "DrLibsGateway: unsupported format for " << path << '\n';
            return nullptr;
        }
    }

    if (!float_data) {
        std::cerr << "DrLibsGateway: failed to load " << path << '\n';
        return nullptr;
    }

    // De-interleave float data into per-channel double vectors.
    const auto num_ch = static_cast<std::size_t>(channels);
    const auto num_frames = static_cast<std::size_t>(total_frames);
    std::vector<Channel> channel_data(num_ch, Channel(num_frames));

    for (std::size_t i = 0; i < num_frames; ++i) {
        for (std::size_t ch = 0; ch < num_ch; ++ch) {
            channel_data[ch][i] = static_cast<double>(
                float_data[i * num_ch + ch]);
        }
    }

    drwav_free(float_data, nullptr);

    return std::make_unique<Sound>(
        std::move(channel_data), static_cast<int>(sample_rate));
}

bool DrLibsGateway::saveSound(const std::string& path, const Sound& sound) {
    drwav_data_format format{};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = static_cast<drwav_uint32>(sound.getNumChannels());
    format.sampleRate = static_cast<drwav_uint32>(sound.getSampleRate());
    format.bitsPerSample = 32;

    drwav wav;
    if (!drwav_init_file_write(
            &wav, path.c_str(), &format, nullptr)) {
        std::cerr << "DrLibsGateway: failed to create " << path << '\n';
        return false;
    }

    // Interleave channels into float buffer.
    const auto& channels = sound.getChannels();
    const auto frames = static_cast<std::size_t>(sound.getNumSamples());
    const auto num_ch = static_cast<std::size_t>(sound.getNumChannels());
    std::vector<float> interleaved(frames * num_ch);

    for (std::size_t i = 0; i < frames; ++i) {
        for (std::size_t ch = 0; ch < num_ch; ++ch) {
            interleaved[i * num_ch + ch] = static_cast<float>(channels[ch][i]);
        }
    }

    drwav_write_pcm_frames(&wav, frames,
                           interleaved.data());
    drwav_uninit(&wav);
    return true;
}

} // namespace audio::adapter::gateway
