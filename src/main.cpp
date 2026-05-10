#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Adapters (outermost layer)
#include "adapter/analysis/MfccAnalyser.h"
#ifdef BRAINIO_HAS_UI
#include "adapter/control/WebSocketParamController.h"
#endif
#include "adapter/effects/FftwSpectralMorph.h"
#include "adapter/fft/PocketfftBackend.h"
#include "adapter/gateway/DrLibsGateway.h"
#include "adapter/gateway/DrLibsRecorder.h"
#include "adapter/playback/MiniaudioOutput.h"

// Domain (innermost layer)
#include "domain/BlockConfig.h"
#include "domain/Brain.h"
#include "domain/Command.h"
#include "domain/SearchParams.h"
#include "domain/Sound.h"

// Use-case layer
#include "usecase/SoundProcessor.h"
#include "usecase/StreamProcessor.h"
#include "usecase/UiHelpers.h"

using audio::ui::resolvePath;
using audio::ui::isAudioFile;
using audio::ui::makeSearch;
using audio::ui::windowFromOrdinal;

// ── CLI helpers ────────────────────────────────────────────────────────
static void printUsage(const char* prog) {
    std::cout << std::format(
        "Usage: {} [mode] [options]\n"
        "\n"
        "Modes (positional, default: batch):\n"
        "  batch      Process target offline, save result to file\n"
        "  stream     Stream target through speakers in real-time (loops)\n"
        "  infinite   Generate infinite landscape from brain sources\n"
        "  ui         Interactive UI mode — control everything from the browser\n"
        "\n"
        "Options:\n"
        "  -i <path>  Brain source sound (repeatable, at least one required)\n"
        "  -d <dir>   Load all audio files in a directory as brain sources\n"
        "  -t <path>  Target sound (required for batch and stream modes)\n"
        "  -o <path>  Output file path (default: sounds/target.wav)\n"
        "  -r <path>  Record stream/infinite output to WAV file\n"
        "  -h         Show this help message\n"
        "\n"
        "Examples:\n"
        "  {} -i sounds/a.wav -i sounds/b.wav -t sounds/target.wav\n"
        "  {} stream -d sounds/brain/ -t sounds/target.wav\n"
        "  {} infinite -d sounds/brain/\n"
        "  {} stream -i sounds/a.wav -t sounds/target.wav -r recording.wav\n"
        "  {} ui -d sounds/SAMPLES/\n",
        prog, prog, prog, prog, prog, prog);
}

// ── Signal handler for graceful Ctrl+C shutdown ────────────────────────
static std::atomic g_quit{false};
static audio::usecase::StreamProcessor* g_stream = nullptr;

static void signalHandler(int /*sig*/) {
    g_quit = true;
    if (g_stream) g_stream->stop();
}

int main(int argc, char* argv[]) {
    // ── CLI argument parsing ───────────────────────────────────────────
    enum class Mode { Batch, Stream, Infinite, Ui };
    auto mode = Mode::Batch;
    std::vector<std::string> brain_paths;
    std::string target_path;
    std::string output_path = "sounds/target.wav";
    std::string recording_path;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        if (arg == "-i") {
            if (++i >= argc) { std::cerr << "Error: -i requires a path argument.\n"; return 1; }
            brain_paths.emplace_back(argv[i]);
            continue;
        }
        if (arg == "-d") {
            if (++i >= argc) { std::cerr << "Error: -d requires a directory path argument.\n"; return 1; }
            const std::filesystem::path dir = resolvePath(argv[i]);
            if (!std::filesystem::is_directory(dir)) {
                std::cerr << std::format("Error: '{}' is not a directory.\n", argv[i]);
                return 1;
            }
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file() && isAudioFile(entry.path())) {
                    brain_paths.push_back(
                        std::filesystem::relative(entry.path(), PROJECT_ROOT).string());
                }
            }
            if (brain_paths.empty()) {
                std::cerr << std::format("Warning: no audio files found in '{}'\n", argv[i]);
            } else {
                std::ranges::sort(brain_paths);
                std::cout << std::format("Loaded {} audio file(s) from '{}'\n",
                                         brain_paths.size(), argv[i]);
            }
            continue;
        }
        if (arg == "-t") {
            if (++i >= argc) { std::cerr << "Error: -t requires a path argument.\n"; return 1; }
            target_path = argv[i];
            continue;
        }
        if (arg == "-o") {
            if (++i >= argc) { std::cerr << "Error: -o requires a path argument.\n"; return 1; }
            output_path = argv[i];
            continue;
        }
        if (arg == "-r") {
            if (++i >= argc) { std::cerr << "Error: -r requires a path argument.\n"; return 1; }
            recording_path = argv[i];
            continue;
        }
        // Positional: mode keyword
        if (arg == "batch")          mode = Mode::Batch;
        else if (arg == "stream")    mode = Mode::Stream;
        else if (arg == "infinite")  mode = Mode::Infinite;
        else if (arg == "ui")        mode = Mode::Ui;
        else {
            std::cerr << std::format("Unknown argument: {}\n", arg);
            printUsage(argv[0]);
            return 1;
        }
    }

    // ── Validate required inputs ───────────────────────────────────────
    if (brain_paths.empty()) {
        std::cerr << "Error: at least one brain source is required (-i <path> or -d <dir>).\n";
        printUsage(argv[0]);
        return 1;
    }
    if (mode != Mode::Infinite && mode != Mode::Ui && target_path.empty()) {
        std::cerr << "Error: a target sound is required (-t <path>) for batch/stream modes.\n";
        printUsage(argv[0]);
        return 1;
    }

    // ── Shared adapters (Composition Root) ─────────────────────────────
    auto fft_backend    = std::make_shared<audio::adapter::fft::PocketfftBackend>();
    auto analyser       = std::make_shared<audio::adapter::analysis::MfccAnalyser>(fft_backend);
    auto spectral_morph = std::make_shared<audio::adapter::effects::SpectralMorph>(fft_backend);
    auto audio_output   = std::make_shared<audio::adapter::playback::MiniaudioOutput>();
    audio::adapter::gateway::DrLibsGateway gateway;

    std::string current_search_name = "closest";
    auto search = makeSearch(current_search_name);

    // ── Block configuration ────────────────────────────────────────────
    audio::BlockConfig source_config{4096, 0, audio::WindowShape::Gaussian};
    audio::BlockConfig target_config{4096, 0, audio::WindowShape::Gaussian};

    // ── Search parameters ──────────────────────────────────────────────
    audio::SearchParams params;
    params.alpha = 1.0;  params.stickyness = 0.6;  params.overlap = 0;
    params.usage_falloff = 1.0;  params.usage_weight = 0.8;
    params.blend_ratio = 1.0;  params.n_ratio = 0.7;
    params.spectral_start = 0;  params.spectral_end = 100;
    params.momentum = 0.0;  params.momentum_decay = 0.95;
    params.grain_size = 1.0;  params.grain_scatter = 0.0;  params.grain_density = 1.0;
    params.grain_size_variation = 0.1;  params.grain_amp_variation = 0.3;
    params.grain_pitch_jitter = 0.2;  params.grain_hop_randomness = 0.2;
    params.spectral_morph = 0.2;
    params.stutter_chance = 0.0;  params.stutter_count = 2;
    params.envelope_shape = 0;  params.envelope_amount = 0.0;

    int num_synapses = 1000;

    // ── Helper: load sounds into brain ─────────────────────────────────
    auto buildBrain = [&]() -> audio::Brain {
        audio::Brain brain(analyser, search, source_config);
        for (const auto& rel_path : brain_paths) {
            const std::string full_path = resolvePath(rel_path);
            auto sound = gateway.loadSound(full_path);
            if (!sound) {
                std::cerr << std::format("Failed to load brain sound: {}\n", full_path);
                continue;
            }
            std::cout << std::format("Brain source '{}': {} ch, {} samples, {} Hz\n",
                rel_path, sound->getNumChannels(), sound->getNumSamples(), sound->getSampleRate());
            brain.addSound(*sound, rel_path);
        }
        if (current_search_name == "synaptic" || current_search_name == "markov") {
            std::cout << std::format("Building synapses ({})...\n", num_synapses);
            brain.buildSynapses(static_cast<std::size_t>(num_synapses));
        }
        return brain;
    };

    audio::Brain brain = buildBrain();

    if (brain.empty()) {
        std::cerr << "Brain is empty — no usable source sounds were loaded.\n";
        return 1;
    }
    std::cout << std::format("Brain ready: {} blocks\n", brain.size());

    // ── Probe sample rate and channels from first source ───────────────
    int default_sr = 44100;
    int default_ch = 2;
    if (!brain.blocks().empty() && !brain.sources().empty()) {
        if (auto probe = gateway.loadSound(resolvePath(brain_paths[0]))) {
            default_sr = probe->getSampleRate();
            default_ch = probe->getNumChannels();
        }
    }

    std::signal(SIGINT, signalHandler);

    // ════════════════════════════════════════════════════════════════════
    // ── Mode: UI (interactive browser control) ─────────────────────────
    // ════════════════════════════════════════════════════════════════════
#ifdef BRAINIO_HAS_UI
    if (mode == Mode::Ui) {
        std::cout << "Starting UI mode (Ctrl+C to quit)...\n";

        auto param_ctrl = std::make_shared<audio::adapter::control::WebSocketParamController>();
        param_ctrl->setParams(params);

        // Sync initial config state.
        audio::port::IParamController::ConfigState cfg;
        cfg.block_size      = source_config.block_size;
        cfg.overlap         = source_config.overlap;
        cfg.window_shape    = static_cast<int>(source_config.window);
        cfg.search_strategy = current_search_name;
        cfg.num_synapses    = num_synapses;
        cfg.target_path     = target_path;
        cfg.playing         = false;
        cfg.recording       = false;
        param_ctrl->setConfigState(cfg);
        param_ctrl->start();

        std::unique_ptr<audio::usecase::StreamProcessor> streamer;
        std::thread audio_thread;
        bool playing = false;
        bool recording = false;
        std::shared_ptr<audio::adapter::gateway::DrLibsRecorder> recorder;

        // Main event loop: poll commands every 100ms.
        while (!g_quit) {
            while (auto cmd_opt = param_ctrl->pollCommand()) {
                std::visit([&]<typename T0>(T0&& cmd) {
                    using T = std::decay_t<T0>;

                    if constexpr (std::is_same_v<T, audio::StartCommand>) {
                        if (playing) return;
                        params = param_ctrl->getParams();

                        streamer = std::make_unique<audio::usecase::StreamProcessor>(
                            params, target_config, audio_output, spectral_morph,
                            param_ctrl, recorder);
                        g_stream = streamer.get();
                        playing = true;
                        cfg.playing = true;
                        param_ctrl->setConfigState(cfg);

                        // Decide mode: if target_path is set, use stream; otherwise infinite.
                        const std::string tgt = cmd.target_path.empty()
                            ? target_path : cmd.target_path;
                        if (!tgt.empty()) {
                            // Stream mode with a target.
                            std::cout << std::format("▶ Starting stream (target: {})...\n", tgt);
                            cfg.target_path = tgt;
                            param_ctrl->setConfigState(cfg);
                            audio_thread = std::thread([&, tgt] {
                                auto target_sound = gateway.loadSound(resolvePath(tgt));
                                if (!target_sound) {
                                    std::cerr << std::format("Failed to load target: {}\n", tgt);
                                    return;
                                }
                                streamer->stream(brain, *target_sound);
                            });
                        } else {
                            // Infinite mode.
                            std::cout << "▶ Starting infinite landscape...\n";
                            audio_thread = std::thread([&] {
                                streamer->streamInfinite(brain, default_sr, default_ch);
                            });
                        }
                    }
                    else if constexpr (std::is_same_v<T, audio::StopCommand>) {
                        if (!playing) return;
                        std::cout << "■ Stopping playback...\n";
                        streamer->stop();
                        if (audio_thread.joinable()) audio_thread.join();
                        g_stream = nullptr;
                        streamer.reset();
                        playing = false;
                        cfg.playing = false;
                        param_ctrl->setConfigState(cfg);
                    }
                    else if constexpr (std::is_same_v<T, audio::RecordCommand>) {
                        if (cmd.enable && !recording) {
                            recorder = std::make_shared<audio::adapter::gateway::DrLibsRecorder>();
                            std::string path = cmd.path.empty()
                                ? resolvePath("sounds/recording.wav") : resolvePath(cmd.path);
                            if (recorder->open(path, default_sr, default_ch)) {
                                recording = true;
                                cfg.recording = true;
                                param_ctrl->setConfigState(cfg);
                                std::cout << std::format("⏺ Recording to {}\n", path);
                                // Attach to running streamer if playing.
                                if (streamer) streamer->setRecorder(recorder);
                            } else {
                                std::cerr << std::format("Failed to open recording: {}\n", path);
                                recorder.reset();
                            }
                        } else if (!cmd.enable && recording) {
                            // Detach from streamer first, then close.
                            if (streamer) streamer->setRecorder(nullptr);
                            recorder->close();
                            recorder.reset();
                            recording = false;
                            cfg.recording = false;
                            param_ctrl->setConfigState(cfg);
                            std::cout << "⏹ Recording stopped.\n";
                        }
                    }
                    else if constexpr (std::is_same_v<T, audio::RebuildCommand>) {
                        if (playing) {
                            std::cout << "■ Stopping for rebuild...\n";
                            streamer->stop();
                            if (audio_thread.joinable()) audio_thread.join();
                            g_stream = nullptr;
                            streamer.reset();
                            playing = false;
                        }

                        std::cout << std::format(
                            "🔄 Rebuilding brain: block_size={}, window={}, search={}\n",
                            cmd.block_size, cmd.window_shape, cmd.search_strategy);

                        if (!cmd.search_strategy.empty()) {
                            current_search_name = cmd.search_strategy;
                            search = makeSearch(current_search_name);
                        }

                        // Only rebuild if block config changed; otherwise just swap strategy.
                        const bool config_changed =
                            cmd.block_size != source_config.block_size
                            || cmd.overlap != source_config.overlap
                            || windowFromOrdinal(cmd.window_shape) != source_config.window;

                        if (config_changed) {
                            source_config.block_size = cmd.block_size;
                            source_config.overlap    = cmd.overlap;
                            source_config.window     = windowFromOrdinal(cmd.window_shape);
                            target_config.block_size = cmd.block_size;
                            target_config.overlap    = cmd.overlap;
                            target_config.window     = windowFromOrdinal(cmd.window_shape);
                            num_synapses             = cmd.num_synapses;
                            brain = buildBrain();
                            std::cout << std::format("Brain rebuilt: {} blocks\n", brain.size());
                        } else {
                            brain.setSearchStrategy(search);
                            std::cout << std::format("Search strategy set to '{}'\n", current_search_name);
                        }

                        cfg.block_size      = cmd.block_size;
                        cfg.overlap         = cmd.overlap;
                        cfg.window_shape    = cmd.window_shape;
                        cfg.search_strategy = current_search_name;
                        cfg.num_synapses    = num_synapses;
                        cfg.playing         = false;
                        param_ctrl->setConfigState(cfg);
                    }
                }, *cmd_opt);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Cleanup on Ctrl+C.
        if (playing) {
            streamer->stop();
            if (audio_thread.joinable()) audio_thread.join();
        }
        if (recorder && recorder->isOpen()) recorder->close();
        param_ctrl->stop();
        std::cout << "\nUI mode stopped.\n";
        return 0;
    }
#else
    if (mode == Mode::Ui) {
        std::cerr << "Error: UI mode requires BRAINIO_BUILD_UI=ON at build time.\n";
        return 1;
    }
#endif

    // ════════════════════════════════════════════════════════════════════
    // ── Mode: Infinite landscape ───────────────────────────────────────
    // ════════════════════════════════════════════════════════════════════
    if (mode == Mode::Infinite) {
        std::cout << "Starting infinite landscape (Ctrl+C to stop)...\n";

        std::shared_ptr<audio::port::IParamController> param_ctrl;
#ifdef BRAINIO_HAS_UI
        auto ws_ctrl = std::make_shared<audio::adapter::control::WebSocketParamController>();
        ws_ctrl->setParams(params);
        ws_ctrl->start();
        param_ctrl = ws_ctrl;
#endif

        std::shared_ptr<audio::adapter::gateway::DrLibsRecorder> recorder;
        if (!recording_path.empty()) {
            recorder = std::make_shared<audio::adapter::gateway::DrLibsRecorder>();
            if (const std::string rec_full = resolvePath(recording_path);
                recorder->open(rec_full, default_sr, default_ch)) {
                std::cout << std::format("Recording to {}\n", rec_full);
            } else {
                std::cerr << std::format("Failed to open recording file: {}\n",
                                          resolvePath(recording_path));
                recorder.reset();
            }
        }

        auto streamer = std::make_unique<audio::usecase::StreamProcessor>(
            params, target_config, audio_output, spectral_morph, param_ctrl, recorder);
        g_stream = streamer.get();

        streamer->streamInfinite(brain, default_sr, default_ch);
#ifdef BRAINIO_HAS_UI
        if (param_ctrl) {
            std::dynamic_pointer_cast<audio::adapter::control::WebSocketParamController>(param_ctrl)->stop();
        }
#endif
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
    std::cout << std::format("Target '{}': {} ch, {} samples, {} Hz\n",
        target_path, target->getNumChannels(), target->getNumSamples(), target->getSampleRate());

    // ════════════════════════════════════════════════════════════════════
    // ── Mode: Real-time streaming ──────────────────────────────────────
    // ════════════════════════════════════════════════════════════════════
    if (mode == Mode::Stream) {
        std::cout << "Streaming in real-time (Ctrl+C to stop)...\n";

        std::shared_ptr<audio::port::IParamController> param_ctrl;
#ifdef BRAINIO_HAS_UI
        auto ws_ctrl = std::make_shared<audio::adapter::control::WebSocketParamController>();
        ws_ctrl->setParams(params);
        ws_ctrl->start();
        param_ctrl = ws_ctrl;
#endif

        std::shared_ptr<audio::adapter::gateway::DrLibsRecorder> recorder;
        if (!recording_path.empty()) {
            recorder = std::make_shared<audio::adapter::gateway::DrLibsRecorder>();
            const std::string rec_full = resolvePath(recording_path);
            if (recorder->open(rec_full, target->getSampleRate(), target->getNumChannels())) {
                std::cout << std::format("Recording to {}\n", rec_full);
            } else {
                std::cerr << std::format("Failed to open recording file: {}\n", rec_full);
                recorder.reset();
            }
        }

        auto streamer = std::make_unique<audio::usecase::StreamProcessor>(
            params, target_config, audio_output, spectral_morph, param_ctrl, recorder);
        g_stream = streamer.get();

        if (!streamer->stream(brain, *target)) {
            std::cerr << "Failed to open audio device.\n";
            return 1;
        }
#ifdef BRAINIO_HAS_UI
        if (param_ctrl) {
            std::dynamic_pointer_cast<audio::adapter::control::WebSocketParamController>(param_ctrl)->stop();
        }
#endif
        g_stream = nullptr;
        std::cout << "Stream finished.\n";
        return 0;
    }

    // ════════════════════════════════════════════════════════════════════
    // ── Mode: Batch processing (default) ───────────────────────────────
    // ════════════════════════════════════════════════════════════════════
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
