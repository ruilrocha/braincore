#include <format>
#include <iostream>
#include <memory>
#include <algorithm>
#include <numeric>
#include <fstream>

#include "src/Sound.h"
#include "src/gateway/ISoundFileGateway.h"
#include "src/gateway/libsndfile.h"
#include "src/aquila/filter/MelFilterBank.h"
#include "src/aquila/transform/Dct.h"

#include <fftw3.h>

constexpr int BLOCK_SIZE = 4096;
constexpr int NUM_MFCC = 12;

std::vector<std::vector<float>> split_into_blocks(const std::vector<float>& channel) {
    std::vector<std::vector<float>> blocks;
    for (auto i = 0; i + BLOCK_SIZE <= channel.size(); i += BLOCK_SIZE) {
        blocks.emplace_back(channel.begin() + i, channel.begin() + i + BLOCK_SIZE);
    }
    return blocks;
}

auto mfcc_distance = [](const std::vector<double>& a, const std::vector<double>& b) {
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double diff = a[i] - b[i];
        sum += diff * diff;
    }
    return std::sqrt(sum);
};

int main() {
    // DI
    const std::shared_ptr<audio::gateway::ISoundFileGateway> gateway = std::make_shared<audio::gateway::libsndfile>();

    const auto input_file = "/IdeaProjects/brainio/sounds/example_mono.wav";
    const auto output_file = "/IdeaProjects/brainio/sounds/example_mono_reversed.wav";

    const auto sound = gateway->loadSound(input_file);
    if (!sound) {
        std::cerr << "Failed to load sound from " << input_file << "\n";
        return 1;
    }

    std::cout << std::format("{} channels, length {}, sample rate {}\n",
        sound->getNumChannels(), sound->getNumSamples(), sound->getSampleRate());

    // Reverse each channel
    auto channels = sound->getChannels();
    // for (auto& channel : channels) {
    //     std::ranges::reverse(channel);
    // }

    for (auto& channel : channels) {

        auto blocks = split_into_blocks(channel);

        std::vector<std::vector<double>> mfccs;
        for (const auto& block : blocks) {
            std::vector<double> block_double(block.begin(), block.end());
            // FFT, Mel filter, DCT (MFCC extraction) for each block
            const int N = static_cast<int>(block_double.size());
            auto* out = static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * (N/2 + 1)));
            fftw_plan plan = fftw_plan_dft_r2c_1d(N, block_double.data(), out, FFTW_ESTIMATE);
            fftw_execute(plan);

            std::vector<std::complex<double>> mfspec(N/2 + 1);
            for (int k = 0; k < N/2 + 1; ++k) {
                mfspec[k] = std::complex(out[k][0], out[k][1]);
            }

            Aquila::MelFilterBank bank(sound->getSampleRate(), N);
            auto filterOutput = bank.applyAll(mfspec);

            Aquila::Dct dct;
            auto mfcc = dct.dct(filterOutput, NUM_MFCC);

            mfccs.push_back(mfcc);

            fftw_destroy_plan(plan);
            fftw_free(out);
        }

        std::vector<size_t> indices(mfccs.size());
        std::iota(indices.begin(), indices.end(), 0);

        std::ranges::sort(indices.begin(), indices.end(), [&](const size_t i, const size_t j) {
            return mfcc_distance(mfccs[0], mfccs[i]) < mfcc_distance(mfccs[0], mfccs[j]);
        });

        // After sorting indices for each channel
        std::vector<float> sorted_channel;
        for (const auto& idx : indices) {
            sorted_channel.insert(sorted_channel.end(), blocks[idx].begin(), blocks[idx].end());
        }

        // Replace the channel with the sorted version
        channel = std::move(sorted_channel);
    }

    gateway->saveSound(output_file,
        {std::move(channels), sound->getNumSamples(), sound->getNumChannels(), sound->getSampleRate()});

    return 0;
}
