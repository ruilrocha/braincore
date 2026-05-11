# Copilot Coding Agent Onboarding Instructions

## Repository Summary
This repository implements **brain-io**, a C++ audio-mangling program.

The core idea: load one or more **source sounds** into a "Brain", then feed in a **target sound**.
Each fixed-length block of the target is replaced by the best-matching source block, where
"best match" is determined by a pluggable search strategy operating on fingerprint vectors
computed via a pluggable analyser.  Multiple post-processing effects (granular scatter,
spectral morphing) can be applied during reconstruction.

## High-Level Information
- **Project Type:** C++ application — audio processing and mangling.
- **Primary Language:** C++23
- **Build Tool:** Conan (version 2.x), CMake (version 3.24+), **Ninja** (required by Conan presets — `brew install ninja`)
- **Frameworks / Libraries:**
  - **PocketFFT** (header-only) — FFT + DCT for spectral analysis
  - **dr_libs** (header-only) — audio file I/O (WAV, FLAC, MP3)
  - **miniaudio** (v0.11.18) — cross-platform audio playback (real-time output)
  - **ixwebsocket** (v11.4.5) — WebSocket server for real-time parameter control
  - **avcpp** (v2.7.1) — C++ wrapper for FFmpeg; video I/O and encoding (`BRAINIO_BUILD_VIDEO`)
  - **Aquila** (in-tree, `src/aquila/`) — Mel filter bank
- **Header guards:** Use `#pragma once` (not `#ifndef`).
- **Linting/Formatting:** None configured yet; follow Google C++ Style Guide.
- **Testing:** None yet; plan to use Google Test.
- **CI/CD:** GitHub Actions (see `.github/workflows/`)
- **Containerization:** Docker (`.devcontainer/Dockerfile`)

## Build, Test, and Validation Instructions
**Always use Conan to manage dependencies and builds. Do not manually install libraries.**

### Bootstrap/Setup
- C++23-capable compiler required (Clang 16+, GCC 13+, MSVC 17.6+).
- **Ninja** required: `brew install ninja` (macOS) or `apt install ninja-build` (Linux).
- No manual dependency installation — Conan handles everything.

### Build (preferred — Conan preset)
```sh
# Debug — binary at cmake-build-debug/conan/build/Debug/brainio
conan install . --output-folder=cmake-build-debug/conan --build=missing
cmake --preset conan-debug
cmake --build --preset conan-debug

# Release — binary at build/build/Release/brainio
# IMPORTANT: use --output-folder=build (not build/build/Release);
# cmake_layout nests the generators automatically.
conan install . --output-folder=build --build=missing -s build_type=Release
cmake --preset conan-release
cmake --build --preset conan-release
```

The available presets are `conan-debug` and `conan-release` (run `cmake --list-presets`).

### Path Resolution
The CMake build injects `PROJECT_ROOT` as a compile definition so the binary
can resolve relative paths (e.g. `sounds/foo.wav`) regardless of where it runs.
Always use relative paths in `main.cpp`; the `resolvePath()` helper prepends the
project root.

### Run
```sh
./cmake-build-debug/conan/build/Debug/brainio -i sounds/a.wav -t sounds/target.wav
```

### Test
TBD — Google Test integration planned.

### Lint/Format
TBD

### Clean
TBD

---

## Architecture Overview

The program follows a **hexagonal / onion architecture**.  The domain core has
zero dependencies on external libraries or frameworks — it communicates with the
outside world only through **port interfaces** that live inside the domain.

```
main.cpp                                ← Composition Root (wires adapters → ports)
  │
  ├── src/adapter/                      ← ADAPTERS (outermost ring)
  │     ├── analysis/MfccAnalyser       ← implements port::IAnalyser
  │     ├── control/WebSocketParamController ← implements port::IParamController
  │     ├── effects/FftwSpectralMorph   ← implements port::IBlockEffect
  │     ├── gateway/DrLibsGateway       ← implements port::ISoundFileGateway
  │     ├── gateway/DrLibsRecorder      ← implements port::IRecorder
  │     ├── playback/MiniaudioOutput    ← implements port::IAudioOutput
  │     ├── video/FfmpegVideoSource     ← implements port::IVideoSource
  │     ├── video/FfmpegVideoOutput     ← implements port::IVideoOutput
  │     └── search/                     ← implements port::ISearchStrategy
  │           ├── ClosestSearch         ←   brute-force closest match
  │           ├── ReverseSearch         ←   furthest match (glitch effect)
  │           ├── SynapticSearch        ←   graph-walk through pre-computed synapses
  │           ├── RandomSearch          ←   pure random selection
  │           ├── WeightedRandomSearch  ←   softmax-weighted random (temperature param)
  │           ├── MarkovChainSearch     ←   probabilistic walk through synapse graph
  │           ├── MomentumSearch        ←   velocity-based trajectory through timbral space
  │           └── SearchUtils.h        ←   shared utilities (stickify, usage, blended distance)
  │
  ├── src/usecase/                      ← USE-CASE layer
  │     ├── SoundProcessor              ← orchestrates brain→target reconstruction (batch)
  │     ├── StreamProcessor             ← real-time streaming + infinite generative landscapes
  │     └── EffectHelpers               ← shared effect functions (granular, stutter, envelope)
  │
  ├── src/domain/                       ← DOMAIN CORE (innermost ring)
  │     ├── Sound, Block, Brain         ← entities & aggregates
  │     ├── Command                     ← value object (start/stop/record/rebuild commands)
  │     ├── Fingerprints                ← value object (primary + secondary + normalised)
  │     ├── BlockConfig                 ← value object (block_size, overlap, window shape)
  │     ├── WindowFunction              ← pure-math window functions (Hamming, Hann, etc.)
  │     ├── SearchParams                ← value object (all UI-controllable parameters)
  │     ├── Random.h                    ← thread-safe random utilities (replaces std::rand)
  │     ├── SourceSound                 ← metadata for loaded sounds
  │     ├── VideoFrame                  ← RGB24 decoded video frame with timestamp
  │     ├── VideoSegment                ← time-bounded reference to a source video file
  │     ├── constants.h                 ← shared constexpr defaults
  │     └── port/                       ← PORT INTERFACES
  │           ├── IAnalyser             ←   fingerprint computation (compute + analyse)
  │           ├── ISearchStrategy       ←   block-selection algorithm
  │           ├── ISoundFileGateway     ←   file I/O
  │           ├── IAudioOutput          ←   real-time audio playback
  │           ├── IBlockEffect          ←   block-pair effect processing (e.g. spectral morph)
  │           ├── IParamController      ←   live params + commands (pollCommand/getParams/setParams)
  │           ├── IRecorder             ←   incremental audio recording (open/write/close)
  │           ├── IVideoSource          ←   video file reading (loadAudio, readSegment, getInfo)
  │           └── IVideoOutput          ←   video frame writing (onBlock, close)
  │
  ├── web/                              ← BROWSER CONTROL PANEL
  │     └── control-panel.html          ← HTML/JS WebSocket client for live param control
  │
  └── src/aquila/                       ← in-tree DSP library (MelFilterBank)
```

### Dependency Rules
- **domain/** depends on **nothing** outside itself (ports are interfaces inside domain).
- **usecase/** depends on **domain/** types only.
- **adapter/** depends on **domain/port/** interfaces and may depend on external libs
  (PocketFFT, dr_libs, avcpp/FFmpeg, Aquila).
- **main.cpp** is the Composition Root — it wires concrete adapters into domain ports.

### Data Flow

1. `main.cpp` creates adapter instances and injects them as `shared_ptr` into the domain.
2. N source sounds are loaded via `ISoundFileGateway` and fed to `Brain::addSound()`,
   which segments each into blocks (with configurable overlap and window shape),
   fingerprints each block via the injected `IAnalyser::analyse()`, stores both
   raw and normalised fingerprints, and stores the `Block` objects. Trailing blocks
   shorter than `block_size` are zero-padded instead of being discarded.
3. Optionally, `Brain::buildSynapses()` pre-computes a similarity graph for
   `SynapticSearch` and `MarkovChainSearch`.
4. A target sound is loaded (except in infinite mode).
5. Four runtime modes:
   - **Batch:** `SoundProcessor::process(brain, target)` processes the entire target
     offline and saves the result to a file.
   - **Stream:** `StreamProcessor::stream(brain, target)` loops the target
     continuously, processing one block at a time and pushing each to
     `IAudioOutput` for real-time playback. Runs until `stop()` / Ctrl+C.
   - **Infinite:** `StreamProcessor::streamInfinite(brain, sr, ch)` generates audio
     endlessly by walking through the brain's timbral space using an evolving
     search fingerprint with additive drift, forced usage tracking, and
     stuck-detection jiggling to prevent freezing. Outputs stereo by default.
   - **UI:** Interactive browser-controlled mode. `main.cpp` runs an event loop
     that polls `IParamController::pollCommand()` for `Command` variants
     (start/stop/record/rebuild). Playback runs in a background thread;
     rebuild stops playback, re-ingests all sources with new `BlockConfig` and
     search strategy, then waits for the next start command.
6. In stream/infinite/ui modes, a `WebSocketParamController` runs on port 7770,
   allowing real-time parameter updates and commands via the browser control panel.
   `StreamProcessor` snapshots params from the controller each block.
7. Post-processing effects (granular scatter, spectral morphing, stutter, envelope
   shaping) are applied per-block via shared `EffectHelpers`.
8. In batch mode, the reconstructed `Sound` is saved via the gateway.
9. In stream/infinite modes, output audio is optionally teed to an `IRecorder`
   for WAV recording (enabled via `-r <path>`).

---

## Project Layout and Key Files

### `src/domain/` — Domain Core
| File | Description |
|------|-------------|
| `Sound.h / .cpp` | Immutable multi-channel audio container. |
| `Block.h` | Value type: samples, channel_samples, fingerprint, secondary_fingerprint, normalised variants, dominant_freq, usage, synapses, video (optional VideoSegment). |
| `Brain.h / .cpp` | Core aggregate. Constructor: `Brain(analyser, search, BlockConfig)`. Methods: `addSound()`, `findBestMatch()`, `buildSynapses()`, `jiggle()`, `depleteUsage()`, `activateSound()`, `isBlockActive()`. |
| `Command.h` | Value object: `std::variant<StartCommand, StopCommand, RecordCommand, RebuildCommand>`. Used by UI mode to send lifecycle actions from the controller to the main event loop. |
| `Fingerprints.h` | Value object: `primary`, `secondary`, `normalised_primary`, `normalised_secondary`, `dominant_freq`. |
| `BlockConfig.h` | Value object: `block_size`, `overlap`, `window` (WindowShape enum). |
| `WindowFunction.h` | Pure-math utility: `apply()` (7 window shapes), `normalise()` (DC-remove + peak-scale). |
| `SearchParams.h` | All UI-controllable parameters — see SearchParams section below. |
| `Random.h` | Thread-safe random utilities (`rng::randomDouble()`, `rng::randomIndex(n)`) using `std::mt19937`. Replaces all `std::rand()` usage. |
| `constants.h` | `kDefaultBlockSize` (4096), `kDefaultNumMfcc` (12), `kDefaultMelBankSize` (24), `kDefaultAlpha` (1.0). |
| `VideoFrame.h` | RGB24 decoded video frame: `width`, `height`, `pixels`, `timestamp_seconds`. `VideoFrame::black(w,h)` creates a zeroed frame for audio-only blocks. |
| `VideoSegment.h` | `VideoSegment` — `source_path`, `offset_seconds`, `duration_seconds`. Stored on Block when sourced from video. `VideoMetadata` — associates a loaded audio track with its originating video file. |
| `port/IAnalyser.h` | Port: `compute(block, sr)` → primary fingerprint; `analyse(block, sr)` → full Fingerprints bundle; `distance(a, b)` → double. |
| `port/ISearchStrategy.h` | Port: `search(target_fp, blocks, analyser, params, current_idx)` → index. |
| `port/ISoundFileGateway.h` | Port: `loadSound(path)`, `saveSound(path, sound)`. |
| `port/IAudioOutput.h` | Port: `open()`, `write()`, `close()` — real-time audio playback. |
| `port/IBlockEffect.h` | Port: `apply(prev, current, amount)` — block-pair effect processing. |
| `port/IParamController.h` | Port: `start()`, `stop()`, `getParams()`, `setParams()`, `pollCommand()`, `setConfigState()` — live parameter control and command queue. |
| `port/IRecorder.h` | Port: `open(path, sr, ch)`, `write(samples)`, `close()` — incremental audio recording. |
| `port/IVideoSource.h` | Port: `loadAudio(path)` → Sound; `getInfo(path, w, h, fps, dur)` → bool; `readFrame(path, t)` → optional VideoFrame; `readSegment(path, start, end)` → vector of VideoFrames. |
| `port/IVideoOutput.h` | Port: `onBlock(segment, duration_sec)` — called per output block with optional VideoSegment; `close()` — flush and finalise output. |

### `src/usecase/` — Application Use-Cases
| File | Description |
|------|-------------|
| `SoundProcessor.h / .cpp` | Batch processor. Constructor takes `SearchParams` + `BlockConfig` for the target. `process(brain, target)` returns a new `Sound`. |
| `StreamProcessor.h / .cpp` | Real-time streaming processor. `stream(brain, target)` loops the target forever for real-time playback; `streamInfinite(brain, sr, ch)` generates endless evolving soundscapes with drift + stuck-detection. Accepts optional `IParamController` for live parameter updates and optional `IRecorder` for recording output. Uses `IAudioOutput` port. |
| `EffectHelpers.h / .cpp` | Shared effect functions used by both processors: `granularScatter()`, `applyStutter()`, `applyEnvelope()`, `extractGrain()`. |

### `src/adapter/analysis/` — Analysis Adapters
| File | Description |
|------|-------------|
| `MfccAnalyser.h / .cpp` | Implements `IAnalyser`. Primary = MFCC (timbral envelope), Secondary = FFT magnitude bins (spectral detail). Single FFT pass per `analyse()` call. Euclidean distance. |

### `src/adapter/gateway/` — Gateway Adapters
| File | Description |
|------|-------------|
| `DrLibsGateway.h / .cpp` | Implements `ISoundFileGateway` using dr_libs. Reads/writes WAV, FLAC, MP3. |
| `DrLibsRecorder.h / .cpp` | Implements `IRecorder` using dr_libs. Writes WAV (PCM 24-bit) incrementally for arbitrarily long recordings. |

### `src/adapter/control/` — Control Adapters
| File | Description |
|------|-------------|
| `WebSocketParamController.h / .cpp` | Implements `IParamController` using ixwebsocket. Runs `ix::WebSocketServer` on port 7770. Receives JSON `{"param":"name","value":0.5}` messages, updates mutex-guarded `SearchParams`. Companion HTML/JS control panel at `web/control-panel.html`. |

### `src/adapter/search/` — Search Strategy Adapters
| File | Description |
|------|-------------|
| `SearchUtils.h` | Shared inline utilities: `stickify()`, `applyUsage()`, `fullScore()` (multi-fingerprint blended distance with usage penalty). |
| `ClosestSearch` | Brute-force scan. Supports stickyness + usage penalties. |
| `ReverseSearch` | Picks the *furthest* match — extreme glitch effect. |
| `SynapticSearch` | Deterministic walk through the pre-computed synapse graph. |
| `RandomSearch` | Pure random block selection (no fingerprint comparison). |
| `WeightedRandomSearch` | Softmax-weighted random over all blocks. Temperature param controls entropy. |
| `MarkovChainSearch` | Probabilistic walk through the synapse graph using softmax transition probabilities. Requires `buildSynapses()`. Temperature controls exploration. |
| `MomentumSearch` | Tracks a velocity vector in fingerprint space. Output drifts smoothly through the brain's timbral landscape like a stream. Controlled by `momentum` and `momentum_decay` params. |

### `src/adapter/effects/` — Effects Adapters
| File | Description |
|------|-------------|
| `PocketfftSpectralMorph.h / .cpp` | Implements `IBlockEffect`. Spectral morphing via PocketFFT: interpolates magnitudes and blends phases in the frequency domain with RMS matching. |

### `src/adapter/video/` — Video Adapters (optional, `BRAINIO_BUILD_VIDEO`)
| File | Description |
|------|-------------|
| `FfmpegVideoSource.h / .cpp` | Implements `IVideoSource` using avcpp/FFmpeg. Extracts audio tracks, queries metadata, decodes video frames. Smart seek: avoids per-block seek+flush for sequential reads; H264-safe 2-second pre-roll seek for backward/random jumps. Per-path `VideoCtx` cache with buffered break-frame for efficient time-window reads. |
| `FfmpegVideoOutput.h / .cpp` | Implements `IVideoOutput` using avcpp/FFmpeg. Encodes RGB24 frames to H264/MP4. Accumulator-based frame count ensures video duration exactly tracks submitted audio time. Encoder timebase `1/90000` with explicit per-packet duration fixes the last-frame stts entry. |

### `src/adapter/playback/` — Playback Adapters
| File | Description |
|------|-------------|
| `MiniaudioOutput.h / .cpp` | Implements `IAudioOutput` using miniaudio. Lock-free SPSC ring buffer with atomic indices and condition-variable back-pressure between producer and audio callback thread. |

### `src/aquila/` — In-Tree DSP Library
| File | Description |
|------|-------------|
| `global.h` | Typedefs: `SampleType`, `FrequencyType`, `ComplexType`, `SpectrumType`. |
| `filter/MelFilter.h / .cpp` | Single triangular Mel filter. |
| `filter/MelFilterBank.h / .cpp` | Bank of Mel filters; `applyAll(spectrum)` returns filter energies. |

---

## SearchParams — UI-Controllable Parameters

All parameters are designed to be bound to UI sliders/knobs:

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| `alpha` | [0.0, 1.0] | 1.0 | Source-vs-target blend. 1.0 = full replacement. |
| `stickyness` | [0.0, 1.0] | 0.0 | Temporal coherence bias toward next sequential block. |
| `overlap` | samples | 0 | Block overlap for smoother transitions. |
| `usage_falloff` ("boredom") | [0.0, 1.0] | 1.0 | How fast blocks become available again. |
| `usage_weight` ("novelty") | [0.0, 1.0] | 0.0 | Penalty for re-used blocks. |
| `blend_ratio` | [0.0, 1.0] | 1.0 | Primary-vs-secondary fingerprint blend. |
| `n_ratio` | [0.0, 1.0] | 0.0 | Raw-vs-normalised fingerprint blend. |
| `secondary_start` | int | 0 | Secondary fingerprint comparison range start. |
| `secondary_end` | int | 100 | Secondary fingerprint comparison range end. |
| `momentum` | [0.0, 1.0] | 0.0 | MomentumSearch: trajectory inertia. |
| `momentum_decay` | [0.0, 1.0] | 0.95 | MomentumSearch: velocity decay per step. |
| `grain_size` | [0.01, 1.0] | 1.0 | Granular: grain size as fraction of block. |
| `grain_scatter` | [0.0, 1.0] | 0.0 | Granular: random temporal offset. |
| `grain_density` | [0.1, 4.0] | 1.0 | Granular: overlap density. |
| `grain_size_variation` | [0.0, 1.0] | 0.0 | Granular: per-grain random size deviation. |
| `grain_amp_variation` | [0.0, 1.0] | 0.0 | Granular: per-grain random amplitude scaling. |
| `grain_pitch_jitter` | [0.0, 1.0] | 0.0 | Granular: per-grain playback-speed variation. |
| `grain_hop_randomness` | [0.0, 1.0] | 0.0 | Granular: randomise base hop positions. |
| `spectral_morph` | [0.0, 1.0] | 0.0 | Cross-fade between consecutive blocks with RMS matching. |
| `stutter_chance` | [0.0, 1.0] | 0.0 | Probability of triggering a stutter effect. |
| `stutter_count` | [2, 8] | 2 | Number of stutter repetitions. |
| `envelope_shape` | [0, 4] | 0 | Per-block envelope: 0=none, 1=decay, 2=swell, 3=tremolo, 4=pluck. |
| `envelope_amount` | [0.0, 1.0] | 0.0 | Envelope intensity. |

---

## Key Design Decisions

1. **Hexagonal architecture** — Domain core has zero outward dependencies.
2. **Nine port interfaces** — `IAnalyser`, `ISearchStrategy`, `ISoundFileGateway`,
   `IAudioOutput`, `IBlockEffect`, `IParamController`, `IRecorder`, `IVideoSource`,
   `IVideoOutput` are pure-virtual classes in `domain/port/`.
3. **Generic IAnalyser port** — The port exposes `compute()`, `analyse()`, and
   `distance()` without naming any specific technique (MFCC, FFT).  Concrete
   adapters decide what primary/secondary fingerprints represent.
4. **Fingerprints bundle** — `analyse()` returns a `Fingerprints` struct with
   primary, secondary, and dominant_freq.  The Brain computes both raw and
   normalised variants and stores them on the Block.
5. **BlockConfig for source and target** — Source (brain) and target can have
   independent block sizes, overlaps, and window shapes.
6. **WindowFunction** — Seven shapes (Rectangle, Hamming, Hann, Blackman,
   Bartlett, FlatTop, Gaussian) applied before fingerprinting. Pure domain math.
7. **Seven search strategies** — Interchangeable adapters: ClosestSearch,
   ReverseSearch, SynapticSearch, RandomSearch, WeightedRandomSearch,
   MarkovChainSearch, MomentumSearch.
8. **SearchUtils** — Shared adapter utilities eliminate code duplication across
   search strategies (stickify, applyUsage, fullScore with blended distance).
9. **SearchParams** — Single value object bundles all UI-controllable parameters.
10. **Usage tracking** — "novelty" (usage_weight) and "boredom" (usage_falloff).
11. **EffectHelpers** — Shared effect functions (granularScatter, applyStutter,
    applyEnvelope) used by both SoundProcessor and StreamProcessor.
12. **Granular post-processing** — Hann-enveloped micro-grains with scatter,
    density control, per-grain stochastic variation (size, amplitude, pitch
    jitter, hop randomness), and overlap-add normalisation.
13. **Spectral morphing** — RMS-matched cross-fade between consecutive matched
    blocks for smooth timbral transitions (via IBlockEffect port).
14. **Real-time streaming** — StreamProcessor uses IAudioOutput port with a
    lock-free SPSC ring buffer for real-time playback. The ring buffer uses
    power-of-two sizing with atomic read/write indices; the producer sleeps
    on a condition variable when full (no busy-wait). Supports target-driven
    and infinite modes.
15. **Infinite generative mode** — StreamProcessor walks through timbral space
    using an evolving search fingerprint with random drift, producing endless
    generative soundscapes.
16. **Multi-channel blocks** — Blocks store per-channel samples for stereo
    reconstruction; fingerprinting uses channel 0 only.
17. **Path resolution** — `PROJECT_ROOT` compile definition ensures relative
    paths work regardless of binary location.
18. **`#pragma once`** — Used everywhere instead of `#ifndef` guards.
19. **Thread-safe random** — `Random.h` provides `rng::randomDouble()` and
    `rng::randomIndex()` using thread-local `std::mt19937`, replacing all
    `std::rand()` usage for better quality and thread safety.
20. **Stereo infinite mode** — `streamInfinite()` defaults to stereo (channels=2)
    and probes the actual channel count from source files. Per-channel samples
    are extracted from `match.channel_samples`.
21. **Short block padding** — `Brain::addSound()` pads trailing blocks shorter
    than `block_size` with silence (zeros) instead of discarding them, ensuring
    no audio data is lost at sound boundaries.
22. **WebSocket parameter control** — `WebSocketParamController` adapter runs an
    `ix::WebSocketServer` on port 7770. A companion `web/control-panel.html`
    connects via WebSocket and sends JSON `{"param":"name","value":0.5}` messages.
    `StreamProcessor` snapshots params from the controller every block via
    `activeParams()`, enabling real-time parameter tweaking during playback.
23. **Output recording** — `DrLibsRecorder` adapter implements `IRecorder`,
    writing WAV (PCM 24-bit) incrementally. `StreamProcessor::outputBlock()`
    tees interleaved audio to the recorder. Enabled via `-r <path>` CLI flag.
24. **Video I/O** — `IVideoSource` / `IVideoOutput` ports decouple video logic from
    the domain. Similarity matching is always audio-only (MFCC); the matched block's
    `VideoSegment` is passed to `IVideoOutput::onBlock()` to write the corresponding
    video clip. Audio-only blocks receive `nullopt` → black frame. The CLI adapter
    uses avcpp/FFmpeg; a Swift app can implement the ports natively with AVFoundation.
25. **Video sync accuracy** — `FfmpegVideoOutput` uses an integer-accumulator to emit
    exactly `round(total_audio_time × fps) − frames_already_emitted` frames per block,
    preventing drift over long outputs. Encoder timebase `1/90000`; explicit per-packet
    `duration` in the encoder's actual timebase fixes the stts last-frame entry so all
    frame deltas are uniform (no zero-duration tail frame).
26. **Smart video seek** — `FfmpegVideoSource::readSegment()` avoids per-block seek+flush
    for sequential reads (just decodes forward). For backward/random jumps it seeks to
    `max(0, start − 2s)` for H264 pre-roll, then skips frames until `pts + frame_dur >
    start_seconds`. A `buffered_vf` break-frame prevents redundant seeks between
    adjacent calls.

## Additional Notes
- **Dependencies:** Managed via Conan and `conandata.yml`.
- **Do not manually edit files in `build/` or `cmake-build-debug/` directories.**
- **Adding a brain source:** use `-i <path>` for individual audio files, `-v <path>` for
  video files (audio extracted automatically), or `-d <dir>` to load all audio/video files
  in a directory. Supported audio: WAV, FLAC, OGG, AIF/AIFF, W64, RF64, RAW, CAF, MP3.
  Supported video: any format FFmpeg can decode (MP4, MOV, MKV, …).
- **Video output:** when any `-v` source is present, batch mode automatically writes a
  matched video file alongside the output WAV (same base name, `.mp4` extension).
- **Changing search strategy:** replace `ClosestSearch` with any other search adapter
  in `main.cpp` (one-line swap). For `SynapticSearch` or `MarkovChainSearch`, call
  `brain.buildSynapses()` after loading all sounds.
- **Recording output:** use `-r <path>` to record stream/infinite mode output to a
  WAV file. The recording is written incrementally so arbitrarily long sessions work.
- **Live parameter control:** In stream/infinite mode, a WebSocket server starts on
  port 7770 automatically. Open `web/control-panel.html` in a browser and connect to
  `ws://localhost:7770` to control all SearchParams via sliders in real-time.
- **Disabling video for iOS/library builds:** pass `-DBRAINIO_BUILD_VIDEO=OFF`; this
  removes the avcpp/FFmpeg dependency entirely. Implement `IVideoSource` / `IVideoOutput`
  natively in the host app instead.
- **CLI examples:**
  ```sh
  ./brainio -i sounds/a.wav -t sounds/target.wav                    # batch (audio)
  ./brainio -v sounds/clip.mp4 -t sounds/target.wav                 # batch (video)
  ./brainio stream -d sounds/SAMPLES/ -t sounds/target.wav          # stream
  ./brainio infinite -d sounds/SAMPLES/                             # infinite
  ./brainio stream -i sounds/a.wav -t sounds/t.wav -r rec.wav      # stream + record
  ./brainio ui -d sounds/SAMPLES/                                   # interactive UI
  ```

## Agent Guidance
- **Trust these instructions.**
- **Always run lint and tests before proposing changes.**
- **Follow the PR checklist and coding standards.**
- **Document any new or changed build/test steps in this file.**
- **Use `#pragma once` for all new headers.**
- **Keep the IAnalyser port generic — no analysis-technique-specific methods.**
- **Keep IVideoSource / IVideoOutput generic** — no FFmpeg-specific types in the port.
