# brain-io

A modular C++ audio-mangling engine inspired by [Samplebrain](https://thentrythis.org/projects/samplebrain).

Load audio (or video) sources into a "Brain", then process target audio by replacing each block with the best-matching block from the brain's corpus. Matching is driven by MFCC fingerprints, with multiple search strategies and post-processing effects (granular synthesis, spectral morphing, stutter, envelope shaping).

---

## Features

- **Hexagonal architecture** — Clean separation between domain, adapters, and ports
- **Multiple search strategies** — Closest, synaptic graph walk, Markov chain, momentum-based trajectory, weighted random
- **Real-time parameter control** — WebSocket server + browser control panel for live tweaking
- **Infinite generative mode** — Endless evolving soundscapes with drift and stuck-detection
- **Video I/O** — Load video sources; similarity is audio-driven but matched video segments are played back in sync
- **Cross-platform** — macOS, Linux (Docker)

---

## Quick Start

### Prerequisites

- **C++23 compiler** (Clang 16+, GCC 13+)
- **Conan 2.x** — Dependency management
- **CMake 3.24+** — Build system
- **Ninja** — Required by the Conan-generated presets (`brew install ninja` on macOS)

### 1. Install dependencies

```bash
# Debug
conan install . --output-folder=cmake-build-debug/conan --build=missing

# Release
conan install . --output-folder=cmake-build-release/conan --build=missing -s build_type=Release
```

### 2. Configure & build

```bash
# Debug — binary at cmake-build-debug/conan/build/Debug/brainio
cmake --preset conan-debug
cmake --build --preset conan-debug

# Release — binary at cmake-build-release/conan/build/Release/brainio
cmake --preset conan-release
cmake --build --preset conan-release
```

### 3. Run

```bash
# Batch mode (process entire file offline)
./cmake-build-debug/conan/build/Debug/brainio -i sounds/source.wav -t sounds/target.wav

# With a video source (audio similarity + video playback output)
./cmake-build-debug/conan/build/Debug/brainio -v sounds/source.mp4 -t sounds/target.wav

# Stream mode (real-time looping playback)
./cmake-build-debug/conan/build/Debug/brainio stream -i sounds/source.wav -t sounds/target.wav

# Infinite mode (generative soundscapes)
./cmake-build-debug/conan/build/Debug/brainio infinite -d sounds/SAMPLES/

# UI mode (interactive browser control, auto-starts playback)
./cmake-build-debug/conan/build/Debug/brainio ui -d sounds/SAMPLES/
# Open web/control-panel.html in browser, connect to ws://localhost:7770

# UI mode with video display window
./cmake-build-debug/conan/build/Debug/brainio ui -v sounds/source.mp4 -vout
```

---

## Build Targets

The CMake build produces these library targets (all always built):

- **brainio-core** — Domain logic (zero external dependencies)
- **brainio-fft** — FFT adapter (PocketFFT)
- **brainio-io** — File I/O adapter (dr_libs)
- **brainio-playback** — Audio playback adapter (miniaudio)
- **brainio-ui** — WebSocket parameter control (ixwebsocket)
- **brainio-video** — Video I/O adapter (avcpp/FFmpeg)
- **brainio-display** — Real-time SDL3 video display
- **brainio** — CLI executable

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
  -i <file>   Add individual audio source file to brain
  -v <file>   Add video source to brain (audio extracted for matching;
              video segments played back for matched blocks)
  -d <dir>    Add all audio/video files in directory to brain
  -t <file>   Target audio file (not used in infinite mode)
  -o <file>   Output WAV file path (batch mode, default: sounds/target.wav)
  -r <file>   Record output to WAV file (stream/infinite only)
  -vout       Open SDL video display window (ui mode only)

Examples:
  brainio -i a.wav -i b.wav -t target.wav
  brainio -v source.mp4 -t target.wav
  brainio stream -d sounds/ -t target.wav -r out.wav
  brainio infinite -d sounds/
  brainio ui -d sounds/
  brainio ui -v source.mp4 -vout
```

---

## WebSocket Control Panel

The program starts a WebSocket server on port 7770 in all streaming modes.

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
│ PocketFFT│  │Sound    │  │ Brain   │
│ dr_libs │  │Processor│  │ Block   │
│miniaudio│  │Stream   │  │ Sound   │
│ixwebskt │  │Processor│  │ Ports   │
│FfmpegSrc│  │         │  │         │
│FfmpegOut│  └─────────┘  └─────────┘
│SdlDisplay│
└─────────┘
```

**Dependency Rule**: Domain depends on nothing. Use-cases depend on domain. Adapters depend on domain ports. Main wires it all together.

---

## Dependencies

All managed via Conan:

- **[PocketFFT](https://gitlab.mpcdf.mpg.de/mtr/pocketfft)** — Header-only FFT and DCT
- **[dr_libs](https://github.com/mackron/dr_libs)** — Header-only audio I/O (WAV/FLAC/MP3)
- **[miniaudio](https://miniaud.io)** — Cross-platform audio playback
- **[ixwebsocket](https://github.com/machinezone/IXWebSocket)** — WebSocket server
- **[readerwriterqueue](https://github.com/cameron314/readerwriterqueue)** — Lock-free SPSC ring buffer
- **[avcpp](https://github.com/h4tr3d/avcpp)** — C++ wrapper for FFmpeg (video I/O)
- **[SDL3](https://libsdl.org)** — Real-time video display
- **Aquila** (in-tree, `src/aquila/`) — Mel filter bank

---

## Docker Development

```bash
# Build and enter container
cd .devcontainer
docker compose up -d
docker compose exec dev bash

# Inside container
conan install . --output-folder=cmake-build-debug/conan --build=missing
cmake --preset conan-debug
cmake --build --preset conan-debug
```

---

## Troubleshooting

### "Address already in use" (port 7770)

Another instance is still running. Find and kill it:

```bash
lsof -ti:7770 | xargs kill
```

### Build errors with ixwebsocket

Make sure you installed dependencies with `--build=missing`:

```bash
conan install . --output-folder=cmake-build-debug/conan --build=missing
```

The `ixwebsocket` package requires `mbedtls` which must be built from source on some platforms.

---

## Acknowledgements

This software was inspired by Dave Griffiths' and Aphex Twin's **[Samplebrain](https://thentrythis.org/projects/samplebrain)** project.

---

## License

[Add your license here]
