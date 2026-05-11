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
    │  ClosestSearch   │                   │  VideoFrame   │
    │  SynapticSearch  │                   │  VideoSegment │
    │  MarkovChainSrch │                   │  Ports:       │
    │  MomentumSearch  │                   │   IAnalyser   │
    │  FfmpegVideoSrc  │                   │   ISearchStrat│
    │  FfmpegVideoOut  │                   │   ISoundFileGW│
    │  SdlVideoDisplay │                   │   IAudioOutput│
    │  VideoDisplayOut │                   │   IBlockEffect│
    │  WebSocketParam  │                   │   IParamCtrl  │
    └──────────────────┘                   │   IRecorder   │
                                           │   IVideoSource│
                                           │   IVideoOutput│
                                           │   IVideoDisplay│
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
| `brainio-video` | FfmpegVideoSource, FfmpegVideoOutput | avcpp, FFmpeg (VideoToolbox HW decode on macOS) |
| `brainio-display` | SdlVideoDisplay, VideoDisplayOutput | SDL3 |
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
| `IVideoSource` | Video file reading: audio extraction, metadata, time-windowed frame decode |
| `IVideoOutput` | Video frame writing: per-block callback + finalise |
| `IVideoDisplay` | Real-time video frame display (SDL3; renders NV12/YUV420P/RGB24 natively) |

## Real-Time Audio / Video Design

### Audio Output (MiniaudioOutput)

- **Lock-free SPSC ring buffer** (4 seconds) between the StreamProcessor (producer) and the
  miniaudio callback (consumer). No mutex on the callback path.
- **Never-drop producer**: `write()` retries unconditionally if the ring is full, waiting on a
  condition variable notified by `fillBuffer()`. Dropping even one sample causes an audible hole.
- **Denormal flushing**: set once per audio thread via ARM64 `FPCR.FZ` or x86 `FTZ|DAZ` bits
  to prevent 10–100× FPU slowdowns from near-zero values in crossfade/MFCC computations.
- **Wall-clock interpolation**: `getAudioTimeSec()` extrapolates linearly from the last callback's
  sample count + wall timestamp, providing sub-millisecond accuracy between hardware callbacks
  (~85 ms on macOS) for smooth A/V sync.

### Video Decode (FfmpegVideoSource)

- **VideoToolbox hardware decode** (macOS, H264/HEVC): `avcodec_find_decoder_by_name("h264_videotoolbox")`
  offloads decode to the Apple media engine at near-zero CPU cost. Automatic fallback to
  single-threaded software decode if the codec is unsupported.
- **NV12 fast path**: VideoToolbox produces `AV_PIX_FMT_NV12` frames. `av_hwframe_transfer_data`
  copies them from GPU to CPU memory as NV12 directly into `Nv12Data` domain structs — no
  `sws_scale` conversion. SDL3 renders NV12 natively via `SDL_UpdateNVTexture`, keeping the
  GPU fragment shader responsible for YUV→RGB.
- **YUV420P fast path**: software frames in `AV_PIX_FMT_YUV420P` / `YUVJ420P` are bulk-copied
  into `Yuv420pData` without rescaling. Only unexpected formats fall back to `sws_scale`.

### VideoFrame Pixel Formats

| `VideoFrame::Pixels` variant | SDL texture type | Source |
|------------------------------|-----------------|--------|
| `Yuv420pData` | `SDL_PIXELFORMAT_IYUV` / `SDL_UpdateYUVTexture` | SW decode |
| `Nv12Data`    | `SDL_PIXELFORMAT_NV12` / `SDL_UpdateNVTexture`  | VideoToolbox HW decode |
| `Rgb24Data`   | `SDL_PIXELFORMAT_RGB24` / `SDL_UpdateTexture`   | Legacy/fallback |

## Runtime Search Strategy Swap

The `Brain::setSearchStrategy()` method allows changing the search algorithm at runtime
without rebuilding the brain (no re-fingerprinting needed). If the new strategy requires
the synapse graph (SynapticSearch, MarkovChainSearch), synapses are built lazily on first use.

**Playback is not interrupted.** The StreamProcessor snapshots the active strategy on every
block via `activeParams()`. When the strategy is swapped, the next block simply uses the
new strategy — no stop/restart cycle occurs. Only block configuration changes (block_size,
overlap, window_shape) require stopping playback and rebuilding the brain.
