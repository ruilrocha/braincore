# brain-io

A modular C++ audio-mangling engine inspired by [Samplebrain](https://thentrythis.org/projects/samplebrain).

Load audio sources into a "Brain", then process target audio by replacing each block with the best-matching block from the brain's corpus. Matching is driven by MFCC fingerprints, with multiple search strategies and post-processing effects (granular synthesis, spectral morphing, stutter, envelope shaping).

---

## Features

- **Hexagonal architecture** — Clean separation between domain, adapters, and ports
- **Pluggable backends** — Swap FFT (PocketFFT/FFTW) and audio I/O (dr_libs/libsndfile) via CMake options
- **Multiple search strategies** — Closest, synaptic graph walk, Markov chain, momentum-based trajectory, weighted random
- **Real-time parameter control** — WebSocket server + browser control panel for live tweaking
- **Infinite generative mode** — Endless evolving soundscapes with drift and stuck-detection
- **iOS-ready** — C-API + XCFramework support for Swift integration (AVFoundation for audio I/O)
- **Cross-platform** — macOS, Linux (Docker), iOS (via XCFramework)

---

## Quick Start

### Prerequisites

- **C++23 compiler** (Clang 16+, GCC 13+, MSVC 17.6+)
- **Conan 2.x** — Dependency management
- **CMake 3.24+** — Build system

### 1. Install dependencies

```bash
conan install . --output-folder=build --build=missing
```

### 2. Configure & build

```bash
# Use Conan preset (recommended)
cmake --preset conan-release
cmake --build build/build/Release
```

Or manually:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=build/build/Release/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### 3. Run

```bash
# Batch mode (process entire file offline)
./build/build/Release/brainio -i sounds/source.wav -t sounds/target.wav

# Stream mode (real-time looping playback)
./build/build/Release/brainio stream -i sounds/source.wav -t sounds/target.wav

# Infinite mode (generative soundscapes)
./build/build/Release/brainio infinite -d sounds/SAMPLES/

# UI mode (interactive browser control)
./build/build/Release/brainio ui -d sounds/SAMPLES/
# Open web/control-panel.html in browser, connect to ws://localhost:7770
```

---

## Build Configuration

### Backend Selection

Choose FFT and audio I/O implementations at build time:

```bash
# FFT backend (default: pocketfft)
-DBRAINIO_FFT_BACKEND=pocketfft   # Header-only, no external deps
-DBRAINIO_FFT_BACKEND=fftw        # FFTW3 (faster for large FFTs)

# Audio I/O backend (default: drlibs)
-DBRAINIO_IO_BACKEND=drlibs       # Header-only (WAV/FLAC/MP3)
-DBRAINIO_IO_BACKEND=libsndfile   # libsndfile (more formats)
-DBRAINIO_IO_BACKEND=none         # No file I/O (library-only build for iOS)
```

### Optional Components

```bash
# Audio playback (default: ON)
-DBRAINIO_BUILD_PLAYBACK=ON       # Enables miniaudio real-time output

# WebSocket UI (default: OFF)
-DBRAINIO_BUILD_UI=ON             # Enables WebSocket server + control panel

# CLI executable (default: ON)
-DBRAINIO_BUILD_CLI=ON            # Builds the brainio command-line tool
```

### Build Targets

The CMake build produces these library targets:

- **brainio-core** — Domain logic (zero external dependencies)
- **brainio-fft** — FFT adapter (PocketFFT or FFTW)
- **brainio-io** — File I/O adapter (dr_libs or libsndfile, optional)
- **brainio-playback** — Audio playback adapter (miniaudio, optional)
- **brainio-capi** — C-compatible API for XCFramework/Swift
- **brainio** — CLI executable (optional)

### Example: Library-Only Build for iOS

```bash
conan install . --output-folder=build --build=missing \
  -s os=iOS -s os.version=16.0 -s arch=armv8

cmake -S . -B build \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_TOOLCHAIN_FILE=build/build/Release/generators/conan_toolchain.cmake \
  -DBRAINIO_FFT_BACKEND=pocketfft \
  -DBRAINIO_IO_BACKEND=none \
  -DBRAINIO_BUILD_PLAYBACK=OFF \
  -DBRAINIO_BUILD_UI=OFF \
  -DBRAINIO_BUILD_CLI=OFF

cmake --build build
```

This produces only `libbrainio-core.a`, `libbrainio-fft.a`, and `libbrainio-capi.a`.

---

## XCFramework (iOS/macOS)

Build a universal framework for iOS/macOS with Swift Package Manager support:

```bash
./scripts/build-xcframework.sh
```

Output: `build/BrainIO.xcframework/`

### Use in Xcode

1. Drag `BrainIO.xcframework` into your Xcode project, or
2. Reference via `Package.swift`:

```swift
.package(path: "../brain-io")
```

See `include/brainio.h` for the C API.

---

## Docker Development

```bash
# Build and enter container
cd .devcontainer
docker compose up -d
docker compose exec dev bash

# Inside container
conan install . --output-folder=build --build=missing
cmake --preset conan-release
cmake --build build/build/Release
```

---

## CLI Usage

```
brainio [mode] [options]

Modes:
  (none)      Batch processing (offline)
  stream      Real-time looping playback
  infinite    Generative endless soundscapes
  ui          Interactive mode with WebSocket control

Options:
  -i <file>   Add individual source file to brain
  -d <dir>    Add all audio files in directory to brain
  -t <file>   Target audio file (not used in infinite mode)
  -r <file>   Record output to WAV file (stream/infinite only)

Examples:
  brainio -i a.wav -i b.wav -t target.wav
  brainio stream -d sounds/ -t target.wav -r out.wav
  brainio infinite -d sounds/
  brainio ui -d sounds/
```

---

## WebSocket Control Panel

When built with `-DBRAINIO_BUILD_UI=ON`, the program starts a WebSocket server on port 7770.

1. Run: `./brainio ui -d sounds/`
2. Open: `web/control-panel.html` in a browser
3. Connect to `ws://localhost:7770`
4. Tweak parameters in real-time via sliders

All `SearchParams` are exposed: alpha, stickyness, usage, blend ratios, granular params, spectral morph, stutter, envelopes.

---

## Architecture

```
┌──────────────────────────────────────┐
│  main.cpp (Composition Root)         │
└──────────────────────────────────────┘
                  │
    ┌─────────────┼─────────────┐
    │             │             │
    v             v             v
┌─────────┐  ┌─────────┐  ┌─────────┐
│ Adapters│  │Use-cases│  │ Domain  │
│         │  │         │  │  Core   │
│ FFTW    │  │Sound    │  │ Brain   │
│ PocketFFT│  │Processor│  │ Block   │
│ dr_libs │  │Stream   │  │ Sound   │
│libsndfile│  │Processor│  │ Ports   │
│miniaudio│  │         │  │         │
│ixwebskt │  └─────────┘  └─────────┘
└─────────┘
```

**Dependency Rule**: Domain depends on nothing. Use-cases depend on domain. Adapters depend on domain ports. Main wires it all together.

---

## Dependencies

All managed via Conan:

- **[PocketFFT](https://gitlab.mpcdf.mpg.de/mtr/pocketfft)** — Header-only FFT (default)
- **[FFTW](https://www.fftw.org)** — Fast Fourier Transform (optional)
- **[dr_libs](https://github.com/mackron/dr_libs)** — Header-only audio I/O (default)
- **[libsndfile](https://libsndfile.github.io/libsndfile)** — Audio file I/O (optional)
- **[miniaudio](https://miniaud.io)** — Cross-platform audio playback
- **[ixwebsocket](https://github.com/machinezone/IXWebSocket)** — WebSocket server
- **[readerwriterqueue](https://github.com/cameron314/readerwriterqueue)** — Lock-free SPSC ring buffer
- **Aquila** (in-tree, `src/aquila/`) — Mel filter bank

---

## Acknowledgements

This software was inspired by Dave Griffiths' and Aphex Twin's **[Samplebrain](https://thentrythis.org/projects/samplebrain)** project.

---

## License

[Add your license here]