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
- **Build Tool:** Conan (version 2.0.5), CMake (version 3.24)
- **Frameworks / Libraries:**
  - **libsndfile** (v1.2.2) — audio file I/O (WAV, FLAC, etc.)
  - **FFTW** (v3.3.10) — real-to-complex FFT for spectral analysis
  - **miniaudio** (v0.11.18) — cross-platform audio playback (real-time output)
  - **Aquila** (in-tree, `src/aquila/`) — Mel filter bank and DCT
- **Header guards:** Use `#pragma once` (not `#ifndef`).
- **Linting/Formatting:** None configured yet; follow Google C++ Style Guide.
- **Testing:** None yet; plan to use Google Test.
- **CI/CD:** GitHub Actions (see `.github/workflows/`)
- **Containerization:** Docker (`.devcontainer/Dockerfile`)

## Build, Test, and Validation Instructions
**Always use Conan to manage dependencies and builds. Do not manually install libraries.**

### Bootstrap/Setup
- C++23-capable compiler required (Clang 16+, GCC 13+, MSVC 17.6+).
- No manual dependency installation — Conan handles everything.

### Build
```sh
conan install . --output-folder=build --build=missing
```
Produces the executable at `cmake-build-debug/brainio`.

### Path Resolution
The CMake build injects `PROJECT_ROOT` as a compile definition so the binary
can resolve relative paths (e.g. `sounds/foo.wav`) regardless of where it runs.
Always use relative paths in `main.cpp`; the `resolvePath()` helper prepends the
project root.

### Run
```sh
./cmake-build-debug/brainio
```
Loads brain sources and a target from `sounds/`, writes output to `sounds/target_sound.wav`.

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
  │     ├── effects/FftwSpectralMorph   ← implements port::IBlockEffect
  │     ├── gateway/LibSndFileGateway   ← implements port::ISoundFileGateway
  │     ├── playback/MiniaudioOutput    ← implements port::IAudioOutput
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
  │     ├── Fingerprints                ← value object (primary + secondary + normalised)
  │     ├── BlockConfig                 ← value object (block_size, overlap, window shape)
  │     ├── WindowFunction              ← pure-math window functions (Hamming, Hann, etc.)
  │     ├── SearchParams                ← value object (all UI-controllable parameters)
  │     ├── Random.h                    ← thread-safe random utilities (replaces std::rand)
  │     ├── SourceSound                 ← metadata for loaded sounds
  │     ├── constants.h                 ← shared constexpr defaults
  │     └── port/                       ← PORT INTERFACES
  │           ├── IAnalyser             ←   fingerprint computation (compute + analyse)
  │           ├── ISearchStrategy       ←   block-selection algorithm
  │           ├── ISoundFileGateway     ←   file I/O
  │           ├── IAudioOutput          ←   real-time audio playback
  │           └── IBlockEffect          ←   block-pair effect processing (e.g. spectral morph)
  │
  └── src/aquila/                       ← in-tree DSP library (MelFilterBank, DCT)
```

### Dependency Rules
- **domain/** depends on **nothing** outside itself (ports are interfaces inside domain).
- **usecase/** depends on **domain/** types only.
- **adapter/** depends on **domain/port/** interfaces and may depend on external libs
  (FFTW, libsndfile, Aquila).
- **main.cpp** is the Composition Root — it wires concrete adapters into domain ports.

### Data Flow

1. `main.cpp` creates adapter instances and injects them as `shared_ptr` into the domain.
2. N source sounds are loaded via `ISoundFileGateway` and fed to `Brain::addSound()`,
   which segments each into blocks (with configurable overlap and window shape),
   fingerprints each block via the injected `IAnalyser::analyse()`, stores both
   raw and normalised fingerprints, and stores the `Block` objects.
3. Optionally, `Brain::buildSynapses()` pre-computes a similarity graph for
   `SynapticSearch` and `MarkovChainSearch`.
4. A target sound is loaded (except in infinite mode).
5. Three runtime modes:
   - **Batch:** `SoundProcessor::process(brain, target)` processes the entire target
     offline and saves the result to a file.
   - **Stream:** `StreamProcessor::stream(brain, target)` loops the target
     continuously, processing one block at a time and pushing each to
     `IAudioOutput` for real-time playback. Runs until `stop()` / Ctrl+C.
   - **Infinite:** `StreamProcessor::streamInfinite(brain, sr)` generates audio
     endlessly by walking through the brain's timbral space using an evolving
     search fingerprint with additive drift, forced usage tracking, and
     stuck-detection jiggling to prevent freezing.
6. Post-processing effects (granular scatter, spectral morphing, stutter, envelope
   shaping) are applied per-block via shared `EffectHelpers`.
7. In batch mode, the reconstructed `Sound` is saved via the gateway.

---

## Project Layout and Key Files

### `src/domain/` — Domain Core
| File | Description |
|------|-------------|
| `Sound.h / .cpp` | Immutable multi-channel audio container. |
| `Block.h` | Value type: samples, channel_samples, fingerprint, secondary_fingerprint, normalised variants, dominant_freq, usage, synapses. |
| `Brain.h / .cpp` | Core aggregate. Constructor: `Brain(analyser, search, BlockConfig)`. Methods: `addSound()`, `findBestMatch()`, `buildSynapses()`, `jiggle()`, `depleteUsage()`, `activateSound()`, `isBlockActive()`. |
| `Fingerprints.h` | Value object: `primary`, `secondary`, `normalised_primary`, `normalised_secondary`, `dominant_freq`. |
| `BlockConfig.h` | Value object: `block_size`, `overlap`, `window` (WindowShape enum). |
| `WindowFunction.h` | Pure-math utility: `apply()` (7 window shapes), `normalise()` (DC-remove + peak-scale). |
| `SearchParams.h` | All UI-controllable parameters — see SearchParams section below. |
| `Random.h` | Thread-safe random utilities (`rng::randomDouble()`, `rng::randomIndex(n)`) using `std::mt19937`. Replaces all `std::rand()` usage. |
| `constants.h` | `kDefaultBlockSize` (4096), `kDefaultNumMfcc` (12), `kDefaultMelBankSize` (24), `kDefaultAlpha` (1.0). |
| `port/IAnalyser.h` | Port: `compute(block, sr)` → primary fingerprint; `analyse(block, sr)` → full Fingerprints bundle; `distance(a, b)` → double. |
| `port/ISearchStrategy.h` | Port: `search(target_fp, blocks, analyser, params, current_idx)` → index. |
| `port/ISoundFileGateway.h` | Port: `loadSound(path)`, `saveSound(path, sound)`. |
| `port/IAudioOutput.h` | Port: `open()`, `write()`, `close()` — real-time audio playback. |
| `port/IBlockEffect.h` | Port: `apply(prev, current, amount)` — block-pair effect processing. |

### `src/usecase/` — Application Use-Cases
| File | Description |
|------|-------------|
| `SoundProcessor.h / .cpp` | Batch processor. Constructor takes `SearchParams` + `BlockConfig` for the target. `process(brain, target)` returns a new `Sound`. |
| `StreamProcessor.h / .cpp` | Real-time streaming processor. `stream(brain, target)` loops the target forever for real-time playback; `streamInfinite(brain, sr)` generates endless evolving soundscapes with drift + stuck-detection. Uses `IAudioOutput` port. |
| `EffectHelpers.h / .cpp` | Shared effect functions used by both processors: `granularScatter()`, `applyStutter()`, `applyEnvelope()`, `extractGrain()`. |

### `src/adapter/analysis/` — Analysis Adapters
| File | Description |
|------|-------------|
| `MfccAnalyser.h / .cpp` | Implements `IAnalyser`. Primary = MFCC (timbral envelope), Secondary = FFT magnitude bins (spectral detail). Single FFT pass per `analyse()` call. Euclidean distance. |

### `src/adapter/gateway/` — Gateway Adapters
| File | Description |
|------|-------------|
| `LibSndFileGateway.h / .cpp` | Implements `ISoundFileGateway` using libsndfile. Reads/writes WAV. |

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
| `FftwSpectralMorph.h / .cpp` | Implements `IBlockEffect`. Spectral morphing via FFTW: interpolates magnitudes and blends phases in the frequency domain with RMS matching. |

### `src/adapter/playback/` — Playback Adapters
| File | Description |
|------|-------------|
| `MiniaudioOutput.h / .cpp` | Implements `IAudioOutput` using miniaudio. Ring buffer between caller and audio callback thread. |

### `src/aquila/` — In-Tree DSP Library
| File | Description |
|------|-------------|
| `global.h` | Typedefs: `SampleType`, `FrequencyType`, `ComplexType`, `SpectrumType`. |
| `filter/MelFilter.h / .cpp` | Single triangular Mel filter. |
| `filter/MelFilterBank.h / .cpp` | Bank of Mel filters; `applyAll(spectrum)` returns filter energies. |
| `transform/Dct.h / .cpp` | Discrete Cosine Transform with cosine cache; `dct(data, outputLength)`. |

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
| `spectral_morph` | [0.0, 1.0] | 0.0 | Cross-fade between consecutive blocks with RMS matching. |
| `stutter_chance` | [0.0, 1.0] | 0.0 | Probability of triggering a stutter effect. |
| `stutter_count` | [2, 8] | 2 | Number of stutter repetitions. |
| `envelope_shape` | [0, 4] | 0 | Per-block envelope: 0=none, 1=decay, 2=swell, 3=tremolo, 4=pluck. |
| `envelope_amount` | [0.0, 1.0] | 0.0 | Envelope intensity. |

---

## Key Design Decisions

1. **Hexagonal architecture** — Domain core has zero outward dependencies.
2. **Five port interfaces** — `IAnalyser`, `ISearchStrategy`, `ISoundFileGateway`,
   `IAudioOutput`, `IBlockEffect` are pure-virtual classes in `domain/port/`.
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
    density control, and overlap-add normalisation.
13. **Spectral morphing** — RMS-matched cross-fade between consecutive matched
    blocks for smooth timbral transitions (via IBlockEffect port).
14. **Real-time streaming** — StreamProcessor uses IAudioOutput port with a ring
    buffer for real-time playback. Supports target-driven and infinite modes.
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

## Additional Notes
- **Dependencies:** Managed via Conan and `conandata.yml`.
- **Do not manually edit files in `build/` or `cmake-build-debug/` directories.**
- **Adding a brain source:** add the `.wav` path to the `brain_paths` vector in `main.cpp`.
- **Changing search strategy:** replace `ClosestSearch` with any other search adapter
  in `main.cpp` (one-line swap). For `SynapticSearch` or `MarkovChainSearch`, call
  `brain.buildSynapses()` after loading all sounds.

## Agent Guidance
- **Trust these instructions.**
- **Always run lint and tests before proposing changes.**
- **Follow the PR checklist and coding standards.**
- **Document any new or changed build/test steps in this file.**
- **Use `#pragma once` for all new headers.**
- **Keep the IAnalyser port generic — no analysis-technique-specific methods.**
