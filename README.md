# brain-io

Audio-mangling engine: load source audio into a "Brain", feed it a target, and each block of the target is replaced by the best-matching block from the brain's corpus. The result is a remixed version of the target that sounds like it was played by the source material.

Inspired by [Samplebrain](https://thentrythis.org/projects/samplebrain).

---

## How it works

1. **Load sources** — audio files are segmented into fixed-length blocks (~93ms at 44.1kHz) and fingerprinted with MFCC, Mel energies, spectral bins, and chroma
2. **Build index** — blocks are indexed in a VP-tree for fast nearest-neighbour queries
3. **Process target** — each target block is fingerprinted and matched to the most similar source block; matched blocks are assembled with OLA crossfade output

---

## Build

Requires: **C++23 compiler**, **Conan 2.x**, **CMake 3.24+**, **Ninja**.

```bash
# Install dependencies + generate presets
conan install . --output-folder=cmake-build-debug/conan --build=missing -s build_type=Debug
conan install . --output-folder=build --build=missing -s build_type=Release

# Build Debug
cmake --preset conan-debug
cmake --build --preset conan-debug

# Build Release
cmake --preset conan-release
cmake --build --preset conan-release

# Run tests
./cmake-build-debug/conan/build/Debug/braincore-tests
```

Conan generates `CMakeUserPresets.json` at the repo root; after running both
install commands above, both `conan-debug` and `conan-release` are available.

### CLion setup

- Enable **Use CMake Presets**.
- Select `conan-debug` for development or `conan-release` for optimized builds.
- Leave **CMake options** empty for preset profiles.

---

## Sanitizers

Both sanitizers are available as manual GitHub Actions workflows (**Actions → ASan / MSan → Run workflow**) and can be run locally against the Debug build.

Both workflows use **Clang** — ASan for better symbolisation, MSan because it is Clang-only.

### AddressSanitizer (ASan)

Catches invalid reads/writes, use-after-free, heap/stack overflows, and leaks.

```bash
# Ensure Clang is the active compiler (required for consistency with CI)
export CC=clang CXX=clang++
conan profile detect --force
conan install . --output-folder=cmake-build-debug/conan --build=missing -s build_type=Debug
cmake --preset conan-debug -DENABLE_ASAN=ON
cmake --build --preset conan-debug
ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 \
  ctest --test-dir cmake-build-debug/conan/build/Debug --output-on-failure
```

### MemorySanitizer (MSan)

Catches reads of uninitialised memory. **Requires Clang** — GCC does not implement MSan.

```bash
# Clang is mandatory for MSan
export CC=clang CXX=clang++
conan profile detect --force
conan install . --output-folder=cmake-build-debug/conan --build=missing -s build_type=Debug
cmake --preset conan-debug -DENABLE_MSAN=ON
cmake --build --preset conan-debug
MSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir cmake-build-debug/conan/build/Debug --output-on-failure
```

> **Note:** MSan requires all linked code to be instrumented. False positives may appear from uninstrumented third-party libraries.

---

## Swift / iOS Library

The `Package.swift` at the repo root exposes a `BrainCore` Swift package via C++ interop.

### Add as dependency

```swift
.package(path: "../brain-io")
```

### BrainSession API

```swift
import BrainCore

var session = audio.BrainSession()

// Configure before addSamples/buildIndex
session.setBlockSize(4096)
session.setOverlapRatio(0.5)          // OLA crossfade [0, 0.9]; 0.5 = 50%
session.setWindowShape(.Hann)         // OLA synthesis window
session.setSearchStrategy(.VpTree)    // default

// Ingest sources (interleaved PCM)
session.addSamplesInterleaved(ptr, frameCount, channels, sampleRate, "name")
session.buildIndex()

// Per-block playback (step = blockSize × (1 − overlapRatio))
let idx = session.advance(targetPtr, stepCount, sampleRate)   // target-driven
let idx = session.advanceInfinite(sampleRate)                  // generative

// Retrieve matched audio
session.getBlockSamplesInterleaved(idx, outPtr, frameCapacity)

// Effects
session.addEffect(.SpectralMorph)
session.setEffectAmount(.SpectralMorph, 0.7)
session.removeEffect(.SpectralMorph)

// Realtime params (no rebuild needed)
session.setStickyness(0.3)
session.setMfccWeight(0.8)
session.setUsageWeight(0.5)   // novelty
session.setUsageFalloff(0.9)  // boredom
```

Key: when OLA is active (`overlapRatio > 0`), `stepSize() = blockSize × (1 − overlapRatio)`. Always advance and retrieve in steps of `stepSize()`, not `blockSize()`.

### iOS App

The companion iOS app is at [`../brainios/`](../brainios/). It provides a native SwiftUI interface over the `BrainCore` library with file pickers, realtime parameter sliders, spectral morph effect, OLA settings, and infinite generative mode.

---

## Architecture

See [docs/architecture.md](docs/architecture.md) for the full hexagonal architecture breakdown.

---

## Dependencies

All managed via Conan:

| Library                                                | Purpose         |
|--------------------------------------------------------|-----------------|
| [PocketFFT](https://gitlab.mpcdf.mpg.de/mtr/pocketfft) | FFT and DCT     |
| Aquila (in-tree, `src/aquila/`)                        | Mel filter bank |

---

## Acknowledgements

Inspired by Dave Griffiths' and Aphex Twin's [Samplebrain](https://thentrythis.org/projects/samplebrain).
