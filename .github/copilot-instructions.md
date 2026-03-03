# Copilot Coding Agent Onboarding Instructions

## Repository Summary
This repository implements **brain-io**, a C++ audio-mangling program inspired by Dave Griffiths' and Aphex Twin's
[Samplebrain](https://thentrythis.org/projects/samplebrain) project.

The core idea: load one or more **source sounds** into a "Brain", then feed in a **target sound**.
Each fixed-length block of the target is replaced by the best-matching source block, where
"best match" is determined by a pluggable search strategy operating on fingerprint vectors
(MFCC by default) computed via a pluggable analyser.

## High-Level Information
- **Project Type:** C++ application — audio processing and mangling.
- **Primary Language:** C++23
- **Build Tool:** Conan (version 2.0.5), CMake (version 3.24)
- **Frameworks / Libraries:**
  - **libsndfile** (v1.2.2) — audio file I/O (WAV, FLAC, etc.)
  - **FFTW** (v3.3.10) — real-to-complex FFT for spectral analysis
  - **Aquila** (in-tree, `src/aquila/`) — Mel filter bank and DCT
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

### Run
```sh
./cmake-build-debug/brainio
```
By default it loads brain sources and a target from `sounds/`, writes output to `sounds/target_sound.wav`.
Edit `main.cpp` to change the file paths, add more brain sources, or swap search strategies.

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
  │     ├── gateway/LibSndFileGateway   ← implements port::ISoundFileGateway
  │     └── search/                     ← implements port::ISearchStrategy
  │           ├── ClosestSearch         ←   brute-force closest match
  │           ├── ReverseSearch         ←   furthest match (glitch effect)
  │           └── SynapticSearch        ←   graph-walk through pre-computed synapses
  │
  ├── src/usecase/                      ← USE-CASE layer
  │     └── SoundProcessor              ← orchestrates brain→target reconstruction
  │
  ├── src/domain/                       ← DOMAIN CORE (innermost ring)
  │     ├── Sound, Block, Brain         ← entities & aggregates
  │     ├── SearchParams                ← value object
  │     ├── constants.h                 ← shared constexpr defaults
  │     └── port/                       ← PORT INTERFACES
  │           ├── IAnalyser             ←   fingerprint computation
  │           ├── ISearchStrategy       ←   block-selection algorithm
  │           └── ISoundFileGateway     ←   file I/O
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

1. `main.cpp` creates adapter instances (`MfccAnalyser`, `ClosestSearch`,
   `LibSndFileGateway`) and injects them as `shared_ptr` into the domain.
2. N source sounds are loaded via `ISoundFileGateway` and fed to `Brain::addSound()`,
   which segments each into blocks (with configurable overlap), fingerprints each
   block via the injected `IAnalyser`, and stores the `Block` objects.
3. Optionally, `Brain::buildSynapses()` pre-computes a similarity graph for
   `SynapticSearch`.
4. A target sound is loaded.
5. `SoundProcessor::process(brain, target)` splits the target into blocks, computes
   fingerprints via `brain.analyser()`, calls `brain.findBestMatch()` (which
   delegates to the injected `ISearchStrategy`), alpha-blends the results, and
   returns a new `Sound`.
6. The reconstructed `Sound` is saved via the gateway.

---

## Project Layout and Key Files

### `src/domain/` — Domain Core
| File | Description |
|------|-------------|
| `Sound.h / .cpp` | Immutable multi-channel audio container. Sizes derived from data, not stored redundantly. `getChannel(idx)` with bounds checking. |
| `Block.h` | Value type: `{ samples, fingerprint, source_name, usage, synapses }`. |
| `Brain.h / .cpp` | Core aggregate. Constructor: `Brain(analyser, search, block_size, overlap)`. Methods: `addSound()`, `findBestMatch(fp, params)`, `buildSynapses(k)`, `blocks()`, `analyser()`, `blockSize()`, `overlap()`. |
| `SearchParams.h` | Value object: `alpha`, `stickyness`, `overlap`, `usage_falloff`, `usage_weight`. |
| `constants.h` | `kDefaultBlockSize` (4096), `kDefaultNumMfcc` (12), `kDefaultMelBankSize` (24), `kDefaultAlpha` (1.0). |
| `port/IAnalyser.h` | Port interface: `compute(block, sr)` → fingerprint; `distance(a, b)` → double. |
| `port/ISearchStrategy.h` | Port interface: `search(target_fp, blocks, analyser, params, current_idx)` → index. |
| `port/ISoundFileGateway.h` | Port interface: `loadSound(path)`, `saveSound(path, sound)`. |

### `src/usecase/` — Application Use-Cases
| File | Description |
|------|-------------|
| `SoundProcessor.h / .cpp` | Constructor takes `SearchParams`. `process(brain, target)` returns a new `Sound`. Block size and analyser are read from the Brain. |

### `src/adapter/analysis/` — Analysis Adapters
| File | Description |
|------|-------------|
| `MfccAnalyser.h / .cpp` | Implements `IAnalyser`. Configurable `num_mfcc`. Pipeline: FFTW → Aquila `MelFilterBank` → Aquila `Dct`. Euclidean distance for comparison. |

### `src/adapter/gateway/` — Gateway Adapters
| File | Description |
|------|-------------|
| `LibSndFileGateway.h / .cpp` | Implements `ISoundFileGateway` using libsndfile. Reads/writes WAV. |

### `src/adapter/search/` — Search Strategy Adapters
| File | Description |
|------|-------------|
| `ClosestSearch.h / .cpp` | Brute-force scan: picks the block with the smallest fingerprint distance. Supports **stickyness** (bias toward the next sequential block for temporal coherence) and **usage penalties** (heavily-used blocks get a distance penalty to promote variety). |
| `ReverseSearch.h / .cpp` | Picks the block with the **largest** fingerprint distance — extreme glitch/mangling effect. |
| `SynapticSearch.h / .cpp` | Walks the pre-computed synapse graph (nearest-neighbour list) of the current block. Only evaluates `num_synapses` candidates per step, producing output that evolves smoothly through the brain's timbral space. Requires `Brain::buildSynapses()`. |

### `src/aquila/` — In-Tree DSP Library
| File | Description |
|------|-------------|
| `global.h` | Typedefs: `SampleType`, `FrequencyType`, `ComplexType`, `SpectrumType`. |
| `filter/MelFilter.h / .cpp` | Single triangular Mel filter. |
| `filter/MelFilterBank.h / .cpp` | Bank of Mel filters; `applyAll(spectrum)` returns filter energies. |
| `transform/Dct.h / .cpp` | Discrete Cosine Transform with cosine cache; `dct(data, outputLength)`. |

### Root Files
| File | Description |
|------|-------------|
| `CMakeLists.txt` | CMake build definition — lists all sources, links `SndFile::sndfile` and `fftw::fftw`. |
| `conanfile.py` / `conandata.yml` | Conan package manager configuration. |
| `conan_provider.cmake` | CMake–Conan integration. |
| `sounds/` | Sample audio files for testing (`.wav`). |

---

## Key Design Decisions

1. **Hexagonal architecture** — Domain core has zero outward dependencies.  All
   external concerns (FFTW, libsndfile, concrete algorithms) live in adapter/.
2. **Three port interfaces** — `IAnalyser`, `ISearchStrategy`, `ISoundFileGateway`
   are pure-virtual classes in `domain/port/`, making the domain fully testable
   with mocks and allowing strategy swaps without changing domain code.
3. **Strategy pattern for search** — `ClosestSearch`, `ReverseSearch`, and
   `SynapticSearch` are interchangeable adapters. Swap by changing one line
   in `main.cpp`.
4. **SearchParams value object** — Bundles `alpha`, `stickyness`, `overlap`,
   `usage_falloff`, `usage_weight` into a single transferable config.
5. **Stickyness** — Temporal coherence: the search can prefer the *next*
   sequential block over the globally closest one, reducing block-boundary
   discontinuities.
6. **Usage tracking & depletion** — Each block has a `usage` counter that grows
   when selected and decays each tick. Search strategies can add
   `usage * usage_weight` as a distance penalty to promote output variety.
7. **Block overlap** — Overlapping segmentation (`Brain` constructor `overlap`
   parameter) produces smoother spectral transitions between blocks.
8. **Synapse graph** — `Brain::buildSynapses(k)` pre-computes each block's k
   nearest neighbours. `SynapticSearch` walks this graph for faster / more
   creative matching.
9. **No redundant state** — `Sound` derives `num_samples` and `num_channels`
   from the underlying `channels_` vector.
10. **MFCC matching on channel 0 only** — the same matched block index is applied
    across all channels, preserving stereo coherence.
11. **Alpha blending** — `[0.0, 1.0]`: `1.0` = full source replacement,
    `0.0` = original target.
12. **No copies on match** — `findBestMatch` returns `const Block&`.

## Additional Notes
- **Dependencies:** Managed via Conan and `conandata.yml`.
- **Do not manually edit files in `build/` or `cmake-build-debug/` directories.**
- **Adding a brain source:** add the `.wav` path to the `brain_paths` vector in `main.cpp`.
- **Changing search strategy:** replace `ClosestSearch` with `ReverseSearch` or
  `SynapticSearch` in `main.cpp` (one-line swap). For `SynapticSearch`, call
  `brain.buildSynapses()` after loading all sounds.

## Agent Guidance
- **Trust these instructions.**
- **Always run lint and tests before proposing changes.**
- **Follow the PR checklist and coding standards.**
- **Document any new or changed build/test steps in this file.**
