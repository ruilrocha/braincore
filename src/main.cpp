#include <format>
#include <iostream>
#include <memory>
#include <algorithm>
#include <numeric>
#include <fstream>

#include "domain/Sound.h"
#include "gateway/ISoundFileGateway.h"
#include "gateway/libsndfile.h"
#include "aquila/filter/MelFilterBank.h"
#include "aquila/transform/Dct.h"

#include <fftw3.h>

constexpr int BLOCK_SIZE = 4096;
constexpr float ALPHA = 1.0f; // For cross-fading, if needed
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

std::vector<double> compute_mfcc(const std::vector<float>& block, int sampleRate) {
    const int N = static_cast<int>(block.size());
    std::vector<double> block_double(block.begin(), block.end());

    // FFT
    auto* out = static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * (N/2 + 1)));
    fftw_plan plan = fftw_plan_dft_r2c_1d(N, block_double.data(), out, FFTW_ESTIMATE);
    fftw_execute(plan);

    // Magnitude spectrum
    std::vector<std::complex<double>> mfspec(N/2 + 1);
    for (int k = 0; k < N/2 + 1; ++k) {
        mfspec[k] = std::complex(out[k][0], out[k][1]);
    }

    // Mel filter bank
    Aquila::MelFilterBank bank(sampleRate, N);
    auto filterOutput = bank.applyAll(mfspec);

    // DCT for MFCCs
    Aquila::Dct dct;
    auto mfcc = dct.dct(filterOutput, NUM_MFCC);

    fftw_destroy_plan(plan);
    fftw_free(out);

    return mfcc;
}



int main() {
    // DI
    const std::shared_ptr<audio::gateway::ISoundFileGateway> gateway = std::make_shared<audio::gateway::libsndfile>();

    const auto input_file = "/IdeaProjects/brain-io/sounds/example_mono.wav";
    const auto target_file = "/IdeaProjects/brain-io/sounds/example_stereo.wav";
    const auto output_file = "/IdeaProjects/brain-io/sounds/target_sound.wav";

    const auto inputSound = gateway->loadSound(input_file);
    if (!inputSound) {
        std::cerr << "Failed to load sound from " << input_file << "\n";
        return 1;
    }

    std::cout << std::format("input: {} channels, length {}, sample rate {}\n",
        inputSound->getNumChannels(), inputSound->getNumSamples(), inputSound->getSampleRate());

    const auto targetSound = gateway->loadSound(target_file);
    if (!targetSound) {
        std::cerr << "Failed to load sound from " << target_file << "\n";
        return 1;
    }

    std::cout << std::format("target: {} channels, length {}, sample rate {}\n",
        targetSound->getNumChannels(), targetSound->getNumSamples(), targetSound->getSampleRate());


    auto inputChannels = inputSound->getChannels();
    auto targetChannels = targetSound->getChannels();

    // 2. Split into blocks
    const auto target_blocks = split_into_blocks(targetSound->getChannels()[0]);
    const auto source_blocks = split_into_blocks(inputSound->getChannels()[0]);

    // 3. Compute MFCCs for all blocks
    std::vector<std::vector<double>> target_mfccs, source_mfccs;
    for (const auto& block : target_blocks) {
        target_mfccs.push_back(compute_mfcc(block, targetSound->getSampleRate()));
    }
    for (const auto& block : source_blocks) {
        source_mfccs.push_back(compute_mfcc(block, inputSound->getSampleRate()));
    }

    // 4. For each target block, find the closest source block
    const auto num_blocks = target_blocks.size();
    std::vector<size_t> best_match_indices(num_blocks);
    for (size_t block_idx = 0; block_idx < num_blocks; ++block_idx) {
        double best_dist = std::numeric_limits<double>::max();
        size_t best_idx = 0;
        for (size_t src_idx = 0; src_idx < source_mfccs.size(); ++src_idx) {
            double dist = mfcc_distance(target_mfccs[block_idx], source_mfccs[src_idx]);
            if (dist < best_dist) {
                best_dist = dist;
                best_idx = src_idx;
            }
        }
        best_match_indices[block_idx] = best_idx;
    }

    // 5. Reconstruct the target sound using the best matching source blocks
    for (size_t ch = 0; ch < targetChannels.size(); ++ch) {
        for (size_t block_idx = 0; block_idx < num_blocks; ++block_idx) {
            size_t best_idx = best_match_indices[block_idx];
            auto input_it = inputChannels[ch].begin() + static_cast<int>(best_idx) * BLOCK_SIZE;
            auto target_it = targetChannels[ch].begin() + static_cast<int>(block_idx) * BLOCK_SIZE;
            for (int i = 0; i < BLOCK_SIZE; ++i) {
                target_it[i] = static_cast<float>(
                    ALPHA * input_it[i] + (1.0 - ALPHA) * target_it[i]
                );
            }
        }
    }

    std::cout << "Saving sound\n";
    if (gateway->saveSound(output_file,
        {std::move(targetChannels), targetSound->getNumSamples(), targetSound->getNumChannels(), targetSound->getSampleRate()})) {
        std::cout <<  std::format("Reconstructed sound saved to {}\n", output_file);
    }

    return 0;
}
