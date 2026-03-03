#include <csignal>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Adapters (outermost layer)
#include "adapter/analysis/MfccAnalyser.h"
#include "adapter/effects/FftwSpectralMorph.h"
#include "adapter/gateway/LibSndFileGateway.h"
#include "adapter/playback/MiniaudioOutput.h"
#include "adapter/search/ClosestSearch.h"
// Swap in for different search behaviour:
// #include "adapter/search/ReverseSearch.h"
// #include "adapter/search/SynapticSearch.h"
// #include "adapter/search/RandomSearch.h"
// #include "adapter/search/WeightedRandomSearch.h"
// #include "adapter/search/MarkovChainSearch.h"
// #include "adapter/search/MomentumSearch.h"

// Domain (innermost layer)
#include "domain/BlockConfig.h"
#include "domain/Brain.h"
#include "domain/SearchParams.h"
#include "domain/Sound.h"

// Use-case layer
#include "usecase/SoundProcessor.h"
#include "usecase/StreamProcessor.h"

// ── Path resolution ────────────────────────────────────────────────────
#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

static std::string resolvePath(const std::string& relative) {
    return (std::filesystem::path(PROJECT_ROOT) / relative).string();
}

// ── CLI helpers ────────────────────────────────────────────────────────
static void printUsage(const char* prog) {
    std::cout << std::format(
        "Usage: {} [mode] [options]\n"
        "\n"
        "Modes (positional, default: batch):\n"
        "  batch      Process target offline, save result to file\n"
        "  stream     Stream target through speakers in real-time (loops)\n"
        "  infinite   Generate infinite landscape from brain sources\n"
        "\n"
        "Options:\n"
        "  -i <path>  Brain source sound (repeatable, at least one required)\n"
        "  -t <path>  Target sound (required for batch and stream modes)\n"
        "  -o <path>  Output file path (default: sounds/target.wav)\n"
        "  -h         Show this help message\n"
        "\n"
        "Examples:\n"
        "  {} -i sounds/a.wav -i sounds/b.wav -t sounds/target.wav\n"
        "  {} stream -i sounds/a.wav -t sounds/target.wav\n"
        "  {} infinite -i sounds/a.wav -i sounds/b.wav\n",
        prog, prog, prog, prog);
}

// ── Signal handler for graceful Ctrl+C shutdown ────────────────────────
static audio::usecase::StreamProcessor* g_stream = nullptr;

static void signalHandler(int /*sig*/) {
    if (g_stream) g_stream->stop();
}

int main(int argc, char* argv[]) {
    // ── CLI argument parsing ───────────────────────────────────────────
    enum class Mode { Batch, Stream, Infinite };
    auto mode = Mode::Batch;
    std::vector<std::string> brain_paths;
    std::string target_path;
    std::string output_path = "sounds/target.wav";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        if (arg == "-i") {
            if (++i >= argc) {
                std::cerr << "Error: -i requires a path argument.\n";
                return 1;
            }
            brain_paths.emplace_back(argv[i]);
            continue;
        }
        if (arg == "-t") {
            if (++i >= argc) {
                std::cerr << "Error: -t requires a path argument.\n";
                return 1;
            }
            target_path = argv[i];
            continue;
        }
        if (arg == "-o") {
            if (++i >= argc) {
                std::cerr << "Error: -o requires a path argument.\n";
                return 1;
            }
            output_path = argv[i];
            continue;
        }
        // Positional: mode keyword
        if (arg == "batch")          mode = Mode::Batch;
        else if (arg == "stream")    mode = Mode::Stream;
        else if (arg == "infinite")  mode = Mode::Infinite;
        else {
            std::cerr << std::format("Unknown argument: {}\n", arg);
            printUsage(argv[0]);
            return 1;
        }
    }

    // ── Validate required inputs ───────────────────────────────────────
    if (brain_paths.empty()) {
        std::cerr << "Error: at least one brain source is required (-i <path>).\n";
        printUsage(argv[0]);
        return 1;
    }
    if (mode != Mode::Infinite && target_path.empty()) {
        std::cerr << "Error: a target sound is required (-t <path>) for batch/stream modes.\n";
        printUsage(argv[0]);
        return 1;
    }

    // ── Adapter wiring (Composition Root) ──────────────────────────────
    auto analyser       = std::make_shared<audio::adapter::analysis::MfccAnalyser>();
    auto search         = std::make_shared<audio::adapter::search::ClosestSearch>();
    auto spectral_morph = std::make_shared<audio::adapter::effects::FftwSpectralMorph>();
    auto audio_output   = std::make_shared<audio::adapter::playback::MiniaudioOutput>();
    audio::adapter::gateway::LibSndFileGateway gateway;

    // ── Block configuration ────────────────────────────────────────────
    audio::BlockConfig source_config;
    source_config.block_size = 4096;
    source_config.overlap    = 0;
    source_config.window     = audio::WindowShape::Hann;

    audio::BlockConfig target_config;
    target_config.block_size = 4096;
    target_config.overlap    = 0;
    target_config.window     = audio::WindowShape::Hann;

    // ── Search parameters ──────────────────────────────────────────────
    audio::SearchParams params;
    params.alpha            = 1.0;
    params.stickyness       = 0.6;
    params.overlap          = 0;
    params.usage_falloff    = 1.0;
    params.usage_weight     = 0.8;
    params.blend_ratio      = 1.0;
    params.n_ratio          = 0.7;
    params.secondary_start  = 0;
    params.secondary_end    = 100;
    params.momentum         = 0.0;
    params.momentum_decay   = 0.95;
    params.grain_size       = 1.0;
    params.grain_scatter    = 0.0;
    params.grain_density    = 1.0;
    params.spectral_morph   = 0.0;
    params.stutter_chance   = 0.0;
    params.stutter_count    = 2;
    params.envelope_shape   = 0;
    params.envelope_amount  = 0.0;

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

    // Optional: pre-compute synapse graph for SynapticSearch / MarkovChainSearch.
    // brain.buildSynapses(1000);

    // ── Mode: Infinite landscape ───────────────────────────────────────
    if (mode == Mode::Infinite) {
        std::cout << "Starting infinite landscape (Ctrl+C to stop)...\n";
        auto streamer = std::make_unique<audio::usecase::StreamProcessor>(
            params, target_config, audio_output, spectral_morph);
        g_stream = streamer.get();
        std::signal(SIGINT, signalHandler);

        int sr = 44100;
        if (!brain.blocks().empty() && !brain.sources().empty()) {
            if (auto probe = gateway.loadSound(resolvePath(brain_paths[0])))
                sr = probe->getSampleRate();
        }
        streamer->streamInfinite(brain, sr, 1);
        g_stream = nullptr;
        std::cout << "\nStream stopped.\n";
        return 0;
    }


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

    // ── Mode: Real-time streaming ──────────────────────────────────────
    if (mode == Mode::Stream) {
        std::cout << "Streaming in real-time (Ctrl+C to stop)...\n";
        auto streamer = std::make_unique<audio::usecase::StreamProcessor>(
            params, target_config, audio_output, spectral_morph);
        g_stream = streamer.get();
        std::signal(SIGINT, signalHandler);

        if (!streamer->stream(brain, *target)) {
            std::cerr << "Failed to open audio device.\n";
            return 1;
        }
        g_stream = nullptr;
        std::cout << "Stream finished.\n";
        return 0;
    }

    // ── Mode: Batch processing (default) ───────────────────────────────
    audio::usecase::SoundProcessor processor(params, target_config, spectral_morph);
    audio::Sound result = processor.process(brain, *target);

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
