#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Adapters (outermost layer)
#include "adapter/analysis/MfccAnalyser.h"
#include "adapter/gateway/LibSndFileGateway.h"
#include "adapter/search/ClosestSearch.h"
// Swap in for different search behaviour:
// #include "adapter/search/ReverseSearch.h"
// #include "adapter/search/SynapticSearch.h"
// #include "adapter/search/RandomSearch.h"
// #include "adapter/search/WeightedRandomSearch.h"
// #include "adapter/search/MarkovChainSearch.h"
// #include "adapter/search/MomentumSearch.h"
// #include "adapter/search/TopNPoolSearch.h"

// Domain (innermost layer)
#include "domain/BlockConfig.h"
#include "domain/Brain.h"
#include "domain/SearchParams.h"
#include "domain/Sound.h"

// Use-case layer
#include "adapter/search/MarkovChainSearch.h"
#include "adapter/search/MomentumSearch.h"
#include "adapter/search/SynapticSearch.h"
#include "adapter/search/TopNPoolSearch.h"
#include "usecase/SoundProcessor.h"

// ── Path resolution ────────────────────────────────────────────────────
// PROJECT_ROOT is injected by CMake so we can always resolve paths
// relative to the repository root, regardless of where the binary runs.
#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

static std::string resolvePath(const std::string& relative) {
    return (std::filesystem::path(PROJECT_ROOT) / relative).string();
}

int main() {
    // ── Source paths (relative to project root) ────────────────────────
    const std::vector<std::string> brain_paths = {
        "sounds/1_MALHAO.wav",
        "sounds/7_WHIP.wav",
        "sounds/SOFTSHELL_video.wav",
        // Add more source sounds here, e.g.:
        // "sounds/another_source.wav",
    };

    const std::string target_path = "sounds/ambient.wav";
    const std::string output_path = "sounds/target_sound.wav";

    // ── Adapter wiring (Composition Root) ──────────────────────────────
    auto analyser = std::make_shared<audio::adapter::analysis::MfccAnalyser>();
    auto search   = std::make_shared<audio::adapter::search::SynapticSearch>();
    audio::adapter::gateway::LibSndFileGateway gateway;

    // ── Block configuration ────────────────────────────────────────────
    // Source (brain) config — controls how brain sounds are segmented.
    audio::BlockConfig source_config;
    source_config.block_size = 4096;
    source_config.overlap    = 0;
    source_config.window     = audio::WindowShape::Hann;

    // Target config — controls how the target is segmented for matching.
    // Can differ from source config for creative effects.
    audio::BlockConfig target_config;
    target_config.block_size = 4096;
    target_config.overlap    = 0;
    target_config.window     = audio::WindowShape::Hann;

    // ── Search parameters ──────────────────────────────────────────────
    audio::SearchParams params;
    params.alpha         = 1.0;   // 1.0 = full replacement, 0.0 = keep target
    params.stickyness    = 0.0;   // [0.0, 1.0] temporal coherence bias
    params.overlap       = 0;     // legacy param (prefer BlockConfig.overlap)
    params.usage_falloff    = 1.0;   // "boredom": 1.0 = no depletion, lower = faster variety
    params.usage_weight     = 0.0;   // "novelty": 0.0 = no penalty, higher = penalise reuse
    params.blend_ratio      = 1.0;   // 0.0 = pure secondary, 1.0 = pure primary, blend in between
    params.n_ratio          = 0.0;   // 0.0 = raw fingerprints, 1.0 = normalised (amplitude-invariant)
    params.secondary_start  = 0;     // Secondary fingerprint bin range start
    params.secondary_end    = 100;   // Secondary fingerprint bin range end
    params.momentum         = 0.0;   // MomentumSearch: 0.0 = closest, 1.0 = full inertia
    params.momentum_decay   = 0.95;  // MomentumSearch: velocity decay per step
    params.pool_size        = 5;     // TopNPoolSearch: number of top candidates
    params.grain_size       = 1.0;   // Granular: 1.0 = full block, 0.1 = tiny grains
    params.grain_scatter    = 1.0;   // Granular: 0.0 = sequential, 1.0 = fully scattered
    params.grain_density    = 1.0;   // Granular: 1.0 = normal, >1 = denser texture
    params.spectral_morph   = 0.0;   // Spectral: 0.0 = hard cuts, 1.0 = full morph

    // ── Build the Brain ────────────────────────────────────────────────
    audio::Brain brain(analyser, search, source_config);

    for (const auto& rel_path : brain_paths) {
        const std::string full_path = resolvePath(rel_path);
        auto sound = gateway.loadSound(full_path);
        if (!sound) {
            std::cerr << std::format("Failed to load brain sound: {}\n", full_path);
            continue;
        }
        std::cout << std::format(
            "Brain source '{}': {} ch, {} samples, {} Hz\n",
            rel_path, sound->getNumChannels(),
            sound->getNumSamples(), sound->getSampleRate());
        brain.addSound(*sound, rel_path);
    }

    if (brain.empty()) {
        std::cerr << "Brain is empty — no usable source sounds were loaded.\n";
        return 1;
    }
    std::cout << std::format("Brain ready: {} blocks\n", brain.size());

    // Optional: pre-compute synapse graph for SynapticSearch.
    brain.buildSynapses(1000);

    // ── Load target sound ──────────────────────────────────────────────
    const std::string target_full = resolvePath(target_path);
    auto target = gateway.loadSound(target_full);
    if (!target) {
        std::cerr << std::format("Failed to load target: {}\n", target_full);
        return 1;
    }
    std::cout << std::format(
        "Target '{}': {} ch, {} samples, {} Hz\n",
        target_path, target->getNumChannels(),
        target->getNumSamples(), target->getSampleRate());

    // ── Process ────────────────────────────────────────────────────────
    audio::usecase::SoundProcessor processor(params, target_config);
    audio::Sound result = processor.process(brain, *target);

    // ── Save output ────────────────────────────────────────────────────
    const std::string output_full = resolvePath(output_path);
    std::cout << "Saving reconstructed sound...\n";
    if (gateway.saveSound(output_full, result)) {
        std::cout << std::format("Saved to {}\n", output_full);
    } else {
        std::cerr << std::format("Failed to save output to {}\n", output_full);
        return 1;
    }

    return 0;
}
