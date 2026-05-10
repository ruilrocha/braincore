# Architecture

## Overview

brain-io follows a **hexagonal (ports & adapters) architecture**. The domain core has zero
dependencies on external libraries — it communicates with the outside world only through
port interfaces.

```
┌──────────────────────────────────────────────────────────────────┐
│                    main.cpp (Composition Root)                    │
│           Wires concrete adapters into domain ports              │
└────────────┬───────────────────┬──────────────────┬──────────────┘
             │                   │                  │
    ┌────────▼────────┐  ┌──────▼──────┐  ┌───────▼───────┐
    │    Adapters      │  │  Use-Cases  │  │  Domain Core  │
    │                  │  │             │  │               │
    │  PocketfftBackend│  │ SoundProc.  │  │  Brain        │
    │  DrLibsGateway   │  │ StreamProc. │  │  Block        │
    │  DrLibsRecorder  │  │ EffectHelp. │  │  Sound        │
    │  MiniaudioOutput │  │             │  │  AudioPrint   │
    │  SpectralMorph   │  └─────────────┘  │  SearchParams │
    │  MfccAnalyser    │                   │  BlockConfig  │
    │  ClosestSearch   │                   │  Ports:       │
    │  SynapticSearch  │                   │   IAnalyser   │
    │  MarkovChainSrch │                   │   ISearchStrat│
    │  MomentumSearch  │                   │   ISoundFileGW│
    │  WebSocketParam  │                   │   IAudioOutput│
    └──────────────────┘                   │   IBlockEffect│
                                           │   IParamCtrl  │
                                           │   IRecorder   │
                                           │   IFft        │
                                           └───────────────┘
```

## Dependency Rules

```
Domain  ←──  Use-Cases  ←──  Adapters  ←──  main.cpp
```

- **Domain** (`src/domain/`) depends on **nothing** outside itself. Port interfaces live here.
- **Use-Cases** (`src/usecase/`) depend on domain types only.
- **Adapters** (`src/adapter/`) implement port interfaces and may depend on external libraries.
- **main.cpp** is the Composition Root — it creates concrete adapters and injects them.

## Build Targets

```
┌─────────────────────────────────────────────────────────────┐
│  brainio (CLI executable)                                   │
│  Links: core + fft + io + playback + ui (all optional)      │
└─────────────────────────────────────────────────────────────┘
        │           │          │           │          │
        ▼           ▼          ▼           ▼          ▼
┌───────────┐ ┌─────────┐ ┌────────┐ ┌──────────┐ ┌──────┐
│brainio-fft│ │brainio-io│ │playback│ │brainio-ui│ │ capi │
│ PocketFFT │ │ dr_libs  │ │miniaudio│ │ixwebsocket│ │C API │
└───────────┘ └─────────┘ └────────┘ └──────────┘ └──────┘
        │           │          │           │          │
        └───────────┴──────────┴───────────┴──────────┘
                              │
                    ┌─────────▼──────────┐
                    │   brainio-core     │
                    │  Domain + UseCases │
                    │  + MelFilterBank   │
                    └────────────────────┘
```

| Target | Contents | Dependencies |
|--------|----------|--------------|
| `brainio-core` | Domain, use-cases, MelFilterBank (sparse) | None (zero external deps) |
| `brainio-fft` | PocketfftBackend | pocketfft (header-only) |
| `brainio-io` | DrLibsGateway, DrLibsRecorder | dr_libs (header-only) |
| `brainio-playback` | MiniaudioOutput | miniaudio, readerwriterqueue |
| `brainio-ui` | WebSocketParamController | ixwebsocket |
| `brainio-capi` | C API (brainio.h) | core + fft |
| `brainio` | CLI executable | all of the above |

## Data Flow

### Batch Mode

```
Source files ──→ Brain::addSound() ──→ Segment into Blocks
                                              │
                     ┌────────────────────────┘
                     ▼
              MfccAnalyser::analyse()
              ├── FFT (PocketFFT)
              ├── MelFilterBank → DCT → MFCC (primary print)
              └── FFT magnitude bins (spectral print)
                     │
                     ▼
              Block stored with: samples, mfcc, spectral,
              normalised variants, dominant_freq
                     │
                     ▼
Target file ──→ Segment into Blocks ──→ For each target block:
                                              │
                     ┌────────────────────────┘
                     ▼
              Brain::findBestMatch(target_print)
              ├── ISearchStrategy::search() picks best block
              └── Post-processing: granular, morph, stutter, envelope
                     │
                     ▼
              Output Sound ──→ Save to file
```

### Stream / Infinite Mode

Same as batch, but processes one block at a time and pushes each to `IAudioOutput`
for real-time playback. Parameters can be tweaked live via WebSocket.

## Port Interfaces

| Port | Purpose |
|------|---------|
| `IFft` | FFT forward/inverse and DCT computation |
| `IAnalyser` | Audio print computation + distance metric |
| `ISearchStrategy` | Block selection algorithm |
| `ISoundFileGateway` | Read/write audio files |
| `IAudioOutput` | Real-time audio playback |
| `IBlockEffect` | Block-pair post-processing (spectral morph) |
| `IParamController` | Live parameter updates + command queue |
| `IRecorder` | Incremental WAV recording |

## Runtime Search Strategy Swap

The `Brain::setSearchStrategy()` method allows changing the search algorithm at runtime
without rebuilding the brain (no re-fingerprinting needed). If the new strategy requires
the synapse graph (SynapticSearch, MarkovChainSearch), synapses are built lazily on first use.

**Playback is not interrupted.** The StreamProcessor snapshots the active strategy on every
block via `activeParams()`. When the strategy is swapped, the next block simply uses the
new strategy — no stop/restart cycle occurs. Only block configuration changes (block_size,
overlap, window_shape) require stopping playback and rebuilding the brain.
