#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <thread>
#include <vector>

// Adapters (outermost layer)
#include "adapter/analysis/MfccAnalyser.h"
#include "adapter/control/WebSocketParamController.h"
#include "adapter/display/SdlVideoDisplay.h"
#include "adapter/display/VideoDisplayOutput.h"
#include "adapter/effects/FftwSpectralMorph.h"
#include "adapter/fft/PocketfftBackend.h"
#include "adapter/gateway/DrLibsGateway.h"
#include "adapter/gateway/DrLibsRecorder.h"
#include "adapter/playback/MiniaudioOutput.h"
#include "adapter/video/FfmpegVideoOutput.h"
#include "adapter/video/FfmpegVideoSource.h"

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

using audio::ui::isAudioFile;
using audio::ui::makeSearch;
using audio::ui::resolvePath;
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
        "  -v <path>  Brain source video (audio extracted; video played back for matched blocks)\n"
        "  -d <dir>   Load all audio files in a directory as brain sources\n"
        "  -t <path>  Target sound (required for batch and stream modes)\n"
        "  -o <path>  Output file path (default: sounds/target.wav)\n"
        "  -r <path>  Record stream/infinite output to WAV file\n"
        "  -vout      Open SDL video display window (ui mode only)\n"
        "  -h         Show this help message\n"
        "\n"
        "Examples:\n"
        "  {} -i sounds/a.wav -i sounds/b.wav -t sounds/target.wav\n"
        "  {} stream -d sounds/brain/ -t sounds/target.wav\n"
        "  {} infinite -d sounds/brain/\n"
        "  {} stream -i sounds/a.wav -t sounds/target.wav -r recording.wav\n"
        "  {} ui -d sounds/SAMPLES/\n"
        "  {} ui -v myvideo.mp4 -vout\n",
        prog, prog, prog, prog, prog, prog, prog);
}

// ── Progress bar (batch mode) ──────────────────────────────────────────
static void printProgress(const std::size_t done, const std::size_t total) {
    constexpr int kBarWidth = 40;
    const double frac = (total > 0) ? static_cast<double>(done) / static_cast<double>(total) : 0.0;
    const int filled = static_cast<int>(frac * kBarWidth);
    std::cout << "\r[";
    for (int i = 0; i < kBarWidth; ++i) {
        std::cout << (i < filled ? "█" : "░");
    }
    std::cout << std::format("] {:3.0f}%  ({}/{})", frac * 100.0, done, total);
    std::cout.flush();
    if (done == total) {
        std::cout << '\n';
    }
}

static std::atomic g_quit{false};
static audio::usecase::StreamProcessor* g_stream = nullptr;
static audio::adapter::display::SdlVideoDisplay* g_display = nullptr;

static void signalHandler(int /*sig*/) {
    g_quit = true;
    if (g_stream != nullptr) {
        g_stream->stop();
    }
    if (g_display != nullptr) {
        g_display->close();
    }
}

int main(int argc, char* argv[]) {
    // ── CLI argument parsing ───────────────────────────────────────────
    enum class Mode : std::uint8_t { Batch, Stream, Infinite, Ui };
    auto mode = Mode::Batch;

    struct BrainSource {
        std::string path;
        bool is_video = false;
    };
    std::vector<BrainSource> brain_sources;
    std::string target_path;
    std::string output_path = "sounds/target.wav";
    std::string recording_path;
    bool show_video_display = false;

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
            brain_sources.push_back({.path = argv[i], .is_video = false});
            continue;
        }
        if (arg == "-v") {
            if (++i >= argc) {
                std::cerr << "Error: -v requires a path argument.\n";
                return 1;
            }
            brain_sources.push_back({.path = argv[i], .is_video = true});
            continue;
        }
        if (arg == "-d") {
            if (++i >= argc) {
                std::cerr << "Error: -d requires a directory path argument.\n";
                return 1;
            }
            const std::filesystem::path dir = resolvePath(argv[i]);
            if (!std::filesystem::is_directory(dir)) {
                std::cerr << std::format("Error: '{}' is not a directory.\n", argv[i]);
                return 1;
            }
            std::vector<std::string> paths;
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file() && isAudioFile(entry.path())) {
                    paths.push_back(std::filesystem::relative(entry.path(), PROJECT_ROOT).string());
                }
            }
            if (paths.empty()) {
                std::cerr << std::format("Warning: no audio files found in '{}'\n", argv[i]);
            } else {
                std::ranges::sort(paths);
                for (auto& p : paths) {
                    brain_sources.push_back({.path = std::move(p), .is_video = false});
                }
                std::cout << std::format("Loaded {} audio file(s) from '{}'\n", paths.size(),
                                         argv[i]);
            }
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
        if (arg == "-r") {
            if (++i >= argc) {
                std::cerr << "Error: -r requires a path argument.\n";
                return 1;
            }
            recording_path = argv[i];
            continue;
        }
        if (arg == "-vout") {
            show_video_display = true;
            continue;
        }
        // Positional: mode keyword
        if (arg == "batch") {
            mode = Mode::Batch;
        } else if (arg == "stream") {
            mode = Mode::Stream;
        } else if (arg == "infinite") {
            mode = Mode::Infinite;
        } else if (arg == "ui") {
            mode = Mode::Ui;
        } else {
            std::cerr << std::format("Unknown argument: {}\n", arg);
            printUsage(argv[0]);
            return 1;
        }
    }

    // ── Validate required inputs ───────────────────────────────────────
    if (brain_sources.empty()) {
        std::cerr << "Error: at least one brain source is required (-i <path>, -v <path>, or -d "
                     "<dir>).\n";
        printUsage(argv[0]);
        return 1;
    }
    if (mode != Mode::Infinite && mode != Mode::Ui && target_path.empty()) {
        std::cerr << "Error: a target sound is required (-t <path>) for batch/stream modes.\n";
        printUsage(argv[0]);
        return 1;
    }

    // ── Shared adapters (Composition Root) ─────────────────────────────
    auto fft_backend = std::make_shared<audio::adapter::fft::PocketfftBackend>();
    auto analyser = std::make_shared<audio::adapter::analysis::MfccAnalyser>(fft_backend);
    auto spectral_morph = std::make_shared<audio::adapter::effects::SpectralMorph>(fft_backend);
    auto audio_output = std::make_shared<audio::adapter::playback::MiniaudioOutput>();
    audio::adapter::gateway::DrLibsGateway gateway;

    // Shared video source adapter — reused across all modes.
    auto video_source = std::make_shared<audio::adapter::video::FfmpegVideoSource>(4, 44100);

    std::string current_search_name = "closest";
    // search and synapse_graph are built after brain is loaded (see below)

    // ── Block configuration ────────────────────────────────────────────
    audio::BlockConfig source_config{
        .block_size = 4096, .overlap = 0, .window = audio::WindowShape::Gaussian};
    audio::BlockConfig target_config{
        .block_size = 4096, .overlap = 0, .window = audio::WindowShape::Gaussian};

    // ── Search parameters ──────────────────────────────────────────────
    audio::SearchParams params;
    params.alpha = 1.0;
    params.stickyness = 0.0;
    params.overlap = 0;
    params.usage_falloff = 0.0;
    params.usage_weight = 0.0;
    params.mfcc_weight = 1.0;  // Default: pure MFCC matching.
    params.n_ratio = 1.0;
    params.spectral_start = 0;
    params.spectral_end = 100;
    params.momentum = 0.0;
    params.momentum_decay = 0.95;
    params.grain_size = 1.0;
    params.grain_scatter = 0.0;
    params.grain_density = 1.0;
    params.grain_size_variation = 0.1;
    params.grain_amp_variation = 0.3;
    params.grain_pitch_jitter = 0.2;
    params.grain_hop_randomness = 0.2;
    params.spectral_morph = 0.0;
    params.stutter_chance = 0.0;
    params.stutter_count = 2;
    params.envelope_shape = 0;
    params.envelope_amount = 0.0;

    int num_synapses = 1000;

    // ── Default sample rate / channels (refined after first source loads) ─
    int default_sr = 44100;
    int default_ch = 2;

    // ── Helper: load sounds into brain ─────────────────────────────────
    auto loadBrain = [&]() -> std::shared_ptr<audio::Brain> {
        auto brain = std::make_shared<audio::Brain>(analyser, source_config);
        for (const auto& [path, is_video] : brain_sources) {
            const std::string full_path = resolvePath(path);

            if (is_video) {
                auto sound = video_source->loadAudio(full_path);
                if (!sound) {
                    std::cerr << std::format("Failed to extract audio from video: {}\n", full_path);
                    continue;
                }
                std::cout << std::format("Video source '{}': {} ch, {} samples, {} Hz\n", path,
                                         sound->getNumChannels(), sound->getNumSamples(),
                                         sound->getSampleRate());
                brain->addSound(
                    *sound, path,
                    audio::VideoMetadata{.path = full_path, .start_offset_seconds = 0.0});
                continue;
            }

            auto sound = gateway.loadSound(full_path);
            if (!sound) {
                std::cerr << std::format("Failed to load brain sound: {}\n", full_path);
                continue;
            }
            std::cout << std::format("Brain source '{}': {} ch, {} samples, {} Hz\n", path,
                                     sound->getNumChannels(), sound->getNumSamples(),
                                     sound->getSampleRate());
            brain->addSound(*sound, path);
        }
        return brain;
    };

    // Build the nearest-neighbour index and create the search strategy.
    // All strategies receive the Brain directly at search time — no index
    // pointer injection needed.
    auto buildSearch = [&](const std::shared_ptr<audio::Brain>& b)
        -> std::shared_ptr<audio::port::ISearchStrategy> {
        std::cout << std::format("Building synapses ({})...\n", num_synapses);
        b->buildIndex(static_cast<std::size_t>(num_synapses));
        std::cout << "Synapses built successfully.\n";
        return makeSearch(current_search_name);
    };

    auto brain = loadBrain();

    if (brain->empty()) {
        std::cerr << "Brain is empty — no usable source sounds were loaded.\n";
        return 1;
    }
    std::cout << std::format("Brain ready: {} blocks\n", brain->size());

    auto search = buildSearch(brain);

    // ── Probe sample rate and channels from first source ───────────────
    if (!brain_sources.empty()) {
        const auto& first = brain_sources[0];
        if (first.is_video) {
            if (auto probe = video_source->loadAudio(resolvePath(first.path))) {
                default_sr = probe->getSampleRate();
                default_ch = probe->getNumChannels();
            }
        } else if (auto probe = gateway.loadSound(resolvePath(first.path))) {
            default_sr = probe->getSampleRate();
            default_ch = probe->getNumChannels();
        }
    }

    std::signal(SIGINT, signalHandler);

    // ════════════════════════════════════════════════════════════════════
    // ── Mode: UI (interactive browser control) ─────────────────────────
    // ════════════════════════════════════════════════════════════════════
    if (mode == Mode::Ui) {
        std::cout << "Starting UI mode (Ctrl+C to quit)...\n";

        auto param_ctrl = std::make_shared<audio::adapter::control::WebSocketParamController>();
        param_ctrl->setParams(params);

        // Sync initial config state.
        audio::port::IParamController::ConfigState cfg;
        cfg.block_size = source_config.block_size;
        cfg.overlap = source_config.overlap;
        cfg.window_shape = static_cast<int>(source_config.window);
        cfg.search_strategy = current_search_name;
        cfg.num_synapses = num_synapses;
        cfg.target_path = target_path;
        cfg.playing = false;
        cfg.recording = false;
        param_ctrl->setConfigState(cfg);
        param_ctrl->start();

        std::unique_ptr<audio::usecase::StreamProcessor> streamer;
        std::thread audio_thread;
        bool playing = false;
        bool recording = false;
        std::shared_ptr<audio::adapter::gateway::DrLibsRecorder> recorder;
        // Kept alive between startPlayback calls so the main loop can call renderFrameForTime.
        std::shared_ptr<audio::port::IVideoOutput> current_video_out;

        // ── SDL Video display (UI mode only, requires -vout flag) ─────────
        // sdl_display persists for the whole session; a fresh VideoDisplayOutput
        // (decoder thread) is created on each startPlayback call.
        std::shared_ptr<audio::adapter::display::SdlVideoDisplay> sdl_display;
        const bool has_video_sources =
            std::ranges::any_of(brain_sources, [](const auto& s) { return s.is_video; });
        if (has_video_sources && show_video_display) {
            int vw = 1280;
            int vh = 720;
            double vfps = 25.0;
            if (auto it =
                    std::ranges::find_if(brain_sources, [](const auto& s) { return s.is_video; });
                it != brain_sources.end()) {
                double vdur = 0.0;
                std::ignore = video_source->getInfo(resolvePath(it->path), vw, vh, vfps, vdur);
            }
            std::cout << std::format("Video display: {}x{} @ {:.1f}fps\n", vw, vh, vfps);
            sdl_display = std::make_shared<audio::adapter::display::SdlVideoDisplay>(vw, vh);
            g_display = sdl_display.get();
        }

        // ── Helper: start playback (stream or infinite) ───────────────────
        auto startPlayback = [&](const std::string& tgt) {
            if (playing) {
                return;
            }
            params = param_ctrl->getParams();

            // Fresh VideoDisplayOutput each start so the decoder thread is alive.
            if (sdl_display) {
                current_video_out = std::make_shared<audio::adapter::display::VideoDisplayOutput>(
                    video_source, sdl_display);
            } else {
                current_video_out.reset();
            }
            streamer = std::make_unique<audio::usecase::StreamProcessor>(
                brain, search, params, target_config, audio_output, spectral_morph, param_ctrl,
                recorder, current_video_out);
            g_stream = streamer.get();
            playing = true;
            cfg.playing = true;
            param_ctrl->setConfigState(cfg);

            if (!tgt.empty()) {
                std::cout << std::format("▶ Starting stream (target: {})...\n", tgt);
                cfg.target_path = tgt;
                param_ctrl->setConfigState(cfg);
                audio_thread = std::thread([&, tgt]() noexcept {
                    try {
                        const auto target_sound = gateway.loadSound(resolvePath(tgt));
                        if (!target_sound) {
                            std::println(stderr, "Failed to load target: {}", tgt);
                            return;
                        }
                        streamer->stream(*target_sound);
                    } catch (const std::exception& e) {
                        std::println(stderr, "Stream error: {}", e.what());
                    } catch (...) {
                        std::println(stderr, "Stream error: unknown exception");
                    }
                });
            } else {
                std::cout << "▶ Starting infinite landscape...\n";
                audio_thread = std::thread([&]() noexcept {
                    try {
                        streamer->streamInfinite(default_sr, default_ch);
                    } catch (const std::exception& e) {
                        std::println(stderr, "Infinite stream error: {}", e.what());
                    } catch (...) {
                        std::println(stderr, "Infinite stream error: unknown exception");
                    }
                });
            }
        };

        // ── Helper: stop playback ─────────────────────────────────────────
        auto stopPlayback = [&] {
            if (!playing) {
                return;
            }
            std::cout << "■ Stopping playback...\n";
            streamer->stop();
            if (audio_thread.joinable()) {
                audio_thread.join();
            }
            g_stream = nullptr;
            streamer.reset();
            playing = false;
            cfg.playing = false;
            param_ctrl->setConfigState(cfg);
        };

        // Auto-start on launch.
        startPlayback(target_path);

        // ── Main event loop ───────────────────────────────────────────────
        while (!g_quit) {
            // SDL event polling + render (~60fps when display is active).
            if (sdl_display) {
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (ev.type == SDL_EVENT_QUIT) {
                        g_quit = true;
                        break;
                    }
                }
                // Pull the video frame matching the current audio clock position,
                // then blit it to screen.  Order matters: update frame first.
                if (current_video_out) {
                    current_video_out->renderFrameForTime(audio_output->getAudioTimeSec());
                }
                if (!sdl_display->renderLatestFrame()) {
                    g_quit = true;
                }
            }

            // Poll WebSocket commands (always, even when SDL is active).
            while (auto cmd_opt = param_ctrl->pollCommand()) {
                std::visit(
                    [&]<typename T0>(T0&& cmd) {
                        using T = std::decay_t<T0>;

                        if constexpr (std::is_same_v<T, audio::StartCommand>) {
                            if (playing) {
                                return;
                            }
                            const std::string tgt =
                                cmd.target_path.empty() ? target_path : cmd.target_path;
                            startPlayback(tgt);
                        } else if constexpr (std::is_same_v<T, audio::StopCommand>) {
                            stopPlayback();
                        } else if constexpr (std::is_same_v<T, audio::RecordCommand>) {
                            if (cmd.enable && !recording) {
                                recorder =
                                    std::make_shared<audio::adapter::gateway::DrLibsRecorder>();
                                std::string path = cmd.path.empty()
                                                       ? resolvePath("sounds/recording.wav")
                                                       : resolvePath(cmd.path);
                                if (recorder->open(path, default_sr, default_ch)) {
                                    recording = true;
                                    cfg.recording = true;
                                    param_ctrl->setConfigState(cfg);
                                    std::cout << std::format("⏺ Recording to {}\n", path);
                                    if (streamer) {
                                        streamer->setRecorder(recorder);
                                    }
                                } else {
                                    std::cerr
                                        << std::format("Failed to open recording: {}\n", path);
                                    recorder.reset();
                                }
                            } else if (!cmd.enable && recording) {
                                if (streamer) {
                                    streamer->setRecorder(nullptr);
                                }
                                recorder->close();
                                recorder.reset();
                                recording = false;
                                cfg.recording = false;
                                param_ctrl->setConfigState(cfg);
                                std::cout << "⏹ Recording stopped.\n";
                            }
                        } else if constexpr (std::is_same_v<T, audio::RebuildCommand>) {
                            stopPlayback();

                            std::cout << std::format(
                                "🔄 Rebuilding brain: block_size={}, window={}, search={}\n",
                                cmd.block_size, cmd.window_shape, cmd.search_strategy);

                            if (!cmd.search_strategy.empty()) {
                                current_search_name = cmd.search_strategy;
                            }
                            num_synapses = cmd.num_synapses;

                            const bool config_changed =
                                cmd.block_size != source_config.block_size ||
                                cmd.overlap != source_config.overlap ||
                                windowFromOrdinal(cmd.window_shape) != source_config.window;

                            if (config_changed) {
                                source_config.block_size = cmd.block_size;
                                source_config.overlap = cmd.overlap;
                                source_config.window = windowFromOrdinal(cmd.window_shape);
                                target_config.block_size = cmd.block_size;
                                target_config.overlap = cmd.overlap;
                                target_config.window = windowFromOrdinal(cmd.window_shape);
                                brain = loadBrain();
                                std::cout
                                    << std::format("Brain rebuilt: {} blocks\n", brain->size());
                            } else {
                                brain =
                                    audio::Brain::rebuild(brain->blocks(), analyser, source_config);
                                std::cout << std::format("Search strategy set to '{}'\n",
                                                         current_search_name);
                            }
                            search = buildSearch(brain);

                            cfg.block_size = cmd.block_size;
                            cfg.overlap = cmd.overlap;
                            cfg.window_shape = cmd.window_shape;
                            cfg.search_strategy = current_search_name;
                            cfg.num_synapses = num_synapses;
                            cfg.playing = false;
                            param_ctrl->setConfigState(cfg);
                        }
                    },
                    *cmd_opt);
            }

            // Sleep: ~60fps when SDL display active, else 100ms.
            std::this_thread::sleep_for(sdl_display ? std::chrono::milliseconds(16)
                                                    : std::chrono::milliseconds(100));
        }

        // Cleanup on quit.
        stopPlayback();
        if (recorder && recorder->isOpen()) {
            recorder->close();
        }
        if (sdl_display) {
            g_display = nullptr;
            sdl_display->close();
        }
        param_ctrl->stop();
        std::cout << "\nUI mode stopped.\n";
        return 0;
    }

    // ════════════════════════════════════════════════════════════════════
    // ── Mode: Infinite landscape ───────────────────────────────────────
    // ════════════════════════════════════════════════════════════════════
    if (mode == Mode::Infinite) {
        std::cout << "Starting infinite landscape (Ctrl+C to stop)...\n";

        auto ws_ctrl = std::make_shared<audio::adapter::control::WebSocketParamController>(
            7770, /*silent=*/true);
        ws_ctrl->setParams(params);
        ws_ctrl->start();
        std::shared_ptr<audio::port::IParamController> param_ctrl = ws_ctrl;

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
            brain, search, params, target_config, audio_output, spectral_morph, param_ctrl,
            recorder);
        g_stream = streamer.get();

        streamer->streamInfinite(default_sr, default_ch);

        ws_ctrl->stop();
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
    std::cout << std::format("Target '{}': {} ch, {} samples, {} Hz\n", target_path,
                             target->getNumChannels(), target->getNumSamples(),
                             target->getSampleRate());

    // ════════════════════════════════════════════════════════════════════
    // ── Mode: Real-time streaming ──────────────────────────────════════
    // ════════════════════════════════════════════════════════════════════
    if (mode == Mode::Stream) {
        std::cout << "Streaming in real-time (Ctrl+C to stop)...\n";

        auto ws_ctrl = std::make_shared<audio::adapter::control::WebSocketParamController>(
            7770, /*silent=*/true);
        ws_ctrl->setParams(params);
        ws_ctrl->start();
        std::shared_ptr<audio::port::IParamController> param_ctrl = ws_ctrl;

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
            brain, search, params, target_config, audio_output, spectral_morph, param_ctrl,
            recorder);
        g_stream = streamer.get();

        if (!streamer->stream(*target)) {
            std::cerr << "Failed to open audio device.\n";
            return 1;
        }

        ws_ctrl->stop();
        g_stream = nullptr;
        std::cout << "Stream finished.\n";
        return 0;
    }

    // ════════════════════════════════════════════════════════════════════
    // ── Mode: Batch processing (default) ───────────────────────────────
    // ════════════════════════════════════════════════════════════════════
    std::shared_ptr<audio::port::IVideoOutput> video_out;
    {
        const bool has_video_sources =
            std::ranges::any_of(brain_sources, [](const auto& s) { return s.is_video; });
        if (has_video_sources) {
            auto video_out_path =
                std::filesystem::path(resolvePath(output_path)).replace_extension(".mp4").string();
            int vw = 1280;
            int vh = 720;
            double vfps = 25.0;
            auto it = std::ranges::find_if(brain_sources, [](const auto& s) { return s.is_video; });
            if (it != brain_sources.end()) {
                double vdur = 0.0;
                std::ignore = video_source->getInfo(resolvePath(it->path), vw, vh, vfps, vdur);
            }
            video_out = std::make_shared<audio::adapter::video::FfmpegVideoOutput>(
                video_source, video_out_path, vw, vh, vfps);
            std::cout << std::format("Video output: {}\n", video_out_path);
        }
    }

    audio::usecase::SoundProcessor processor(search, params, target_config, spectral_morph,
                                             video_out);
    std::cout << "Processing...\n";
    audio::Sound result = processor.process(brain, *target, printProgress);
    if (video_out) {
        video_out->close();
    }

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
