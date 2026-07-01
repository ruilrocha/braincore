# Architecture

brain-io follows a **hexagonal (ports & adapters) architecture**. The domain core has zero
external dependencies — it communicates with the outside world only through port interfaces.

## Layers

```
┌─────────────────────────────────────────────────────────┐
│              BrainSession  (src/BrainSession.h/.cpp)    │
│         Swift C++ interop — Pimpl facade only           │
└──────────────────────┬──────────────────────────────────┘
                       │ delegates to
┌──────────────────────▼──────────────────────────────────┐
│               BrainEngine  (src/engine/)                │
│   Reusable C++ API — composes domain + adapters         │
│                                                         │
│  ┌───────────────┐  ┌──────────────┐  ┌─────────────┐  │
│  │    PlayHead   │  │OutputPipeline│  │InfiniteDrift│  │
│  │ (domain cursor│  │ OLA + effects│  │    State    │  │
│  │  + usages)    │  │ chain        │  │ (inf. mode) │  │
│  └───────────────┘  └──────────────┘  └─────────────┘  │
└──────────────────────┬──────────────────────────────────┘
                       │ uses
┌──────────────────────▼──────────────────────────────────┐
│              Domain  (src/domain/)                      │
│                                                         │
│   Brain · Block · BlockAnalysis · NearestNeighbourIndex │
│   BlockConfig · SearchParams · SearchContext            │
│   PlayHead · Sound · WindowFunction · Random            │
│                                                         │
│   Ports (interfaces — no concrete deps):                │
│     IAnalyser · ISearchStrategy · IBlockEffect · IFft   │
└──────────────────────┬──────────────────────────────────┘
                       │ implemented by
┌──────────────────────▼──────────────────────────────────┐
│              Adapters  (src/adapter/)                   │
│                                                         │
│  analysis/  MfccAnalyser          → IAnalyser           │
│  fft/       PocketfftBackend      → IFft                │
│  effects/   SpectralMorph         → IBlockEffect        │
│             OlaBuffer, EffectHelpers (DSP utilities)    │
│  search/    ClosestSearch  → ISearchStrategy            │
│             VpTreeSearch   → ISearchStrategy            │
│             SynapticSearch → ISearchStrategy            │
│             SearchUtils.h  (shared scoring utilities)   │
└─────────────────────────────────────────────────────────┘
```

## Dependency Rules

```
Domain  ←──  Adapters  ←──  BrainEngine  ←──  BrainSession  ←──  Swift app
```

- **Domain** (`src/domain/`) — zero external deps; ports live here as pure-virtual classes
- **Adapters** (`src/adapter/`) — implement domain ports; may use PocketFFT and Aquila
- **Engine** (`src/engine/`) — composes domain + adapters; pure C++ API; no Swift deps
- **BrainSession** (`src/BrainSession.h/.cpp`) — Pimpl facade; the only C++ surface Swift touches

## Key Types

### Domain

| Type | Role |
|------|------|
| `Brain` | Core aggregate. Holds all `Block`s; owns `NearestNeighbourIndex` (VP tree + K-NN). |
| `Block` | Fixed-length audio segment: `channel_samples` (float) + `analysis: BlockAnalysis`. |
| `BlockAnalysis` | Pair of `AudioPrint`s: `print` (raw) + `normalised_print` (amplitude-invariant). |
| `AudioPrint` | `{mfcc, mel, spectral, chroma}` — four `vector<float>` fingerprint vectors. |
| `NearestNeighbourIndex` | VP-tree + precomputed K-NN table in a flat row-major mel matrix. |
| `PlayHead` | Per-stream cursor: current block index + per-block usage counters + strategy. |
| `SearchContext` | Snapshot passed to `ISearchStrategy::search()` each block. |
| `SearchParams` | All realtime-tweakable parameters (weights, stickyness, novelty, …). |
| `BlockConfig` | `{block_size, overlap, window}` — segmentation + OLA synthesis config. |

### Engine

| Type | Role |
|------|------|
| `BrainEngine` | Orchestrates the full advance/output loop; owns Brain + PlayHead + OutputPipeline. |
| `OutputPipeline` | Two-stage output: OlaBuffer (OLA synthesis) → BlockEffectChain (effects). |
| `InfiniteDriftState` | State machine for target-free generative mode: evolves a fingerprint through timbral space with stuck/silence escape. |
| `BlockEffectChain` | Ordered chain of `IBlockEffect` adapters (e.g. SpectralMorph). |
| `AtomicSearchParams` | Relaxed-atomic wrappers around `SearchParams` for UI-thread writes during playback. |
| `BrainEngineTypes` | `SearchStrategy` and `EffectType` enums — stable values for Swift interop. |

## Data Flow

```
Source audio ──→ BrainEngine::addSound()
                     │
                     ▼
              Brain::addSound()
                 ├── segment into fixed-length Blocks (zero-pad trailing)
                 ├── MfccAnalyser::analyse() [parallel threads]
                 │     ├── PocketfftBackend::forwardInto() [thread-local scratch]
                 │     ├── MelFilterBank → mel print
                 │     ├── DCT → mfcc print
                 │     ├── FFT magnitudes → spectral print
                 │     └── Chroma rebinning → chroma print
                 └── mel_matrix_ (flat row-major N×mel_dim for cache-friendly scans)
                     │
                     ▼
              Brain::buildIndex()
                 └── NearestNeighbourIndex::build()
                       ├── VP-tree (O(log N) dynamic queries) — VpTreeSearch
                       └── K-NN table (O(1) synapse lookup) — SynapticSearch

Target block ──→ BrainEngine::advance()
                 1. MfccAnalyser::analyse() on target block
                 2. PlayHead::advance() → ISearchStrategy::search() → best block index
                    ├── SearchUtils::fullScore() = weightedDist + usage penalty + brightness bias
                    └── SearchUtils::stickify() post-pass for temporal coherence
                 3. OutputPipeline::push() — accumulate into OLA buffer
                 4. OutputPipeline::readInterleaved() → output samples
                    ├── OlaBuffer::read() (OLA path)
                    └── BlockEffectChain::apply() (e.g. SpectralMorph)

─── Infinite mode ───────────────────────────────────────────
BrainEngine::advanceInfinite()
    1. InfiniteDriftState::currentTarget() → evolving synthetic target fingerprint
    2. PlayHead::advance() → matched index (same scoring path as above)
    3. InfiniteDriftState::updateFromMatch()
       ├── low-energy escape: reseed if > 8 consecutive near-silent blocks
       ├── stuck escape: reseed if same block wins > 16 consecutive times
       └── next target ← random precomputed neighbour + small noise
    4. OutputPipeline push/read (same as target-driven mode)
```

## OLA Output Synthesis

`OlaBuffer` implements overlap-add (OLA) synthesis:

- MFCC analysis always uses a Hann window internally (hardcoded for fingerprint quality)
- The **OLA synthesis window** is user-selected (`BlockConfig.window`) — applied to raw
  `channel_samples` during output, not during fingerprinting
- At 50% overlap with a Hann window: perfect reconstruction (uniform amplitude)
- `stepSize = blockSize × (1 − overlapRatio)` — advance and retrieve in steps of `stepSize()`
- Internal accumulation uses `double` to avoid float drift over long sessions

## Performance Model

| Hot path | Technique | Benefit |
|----------|-----------|---------|
| Parallel block ingest | `std::thread` worker pool in `Brain::addSound()` | N × FFT time → N/threads |
| Thread-local FFT scratch | `FftScratch` in `PocketfftBackend` | 0 heap allocs per FFT |
| Thread-local analysis scratch | `AnalysisScratch` in `MfccAnalyser` | 0 heap allocs per block analysis |
| Non-allocating FFT | `forwardInto()` writes into caller buffer | Eliminates `vector<ComplexValue>` alloc per call |
| Flat mel matrix | Row-major `N × dim float` in `Brain` | Sequential memory access for O(N) scans → SIMD |
| Hoisted weight normalisation | `NormWeights::from(params)` pre-scan | Eliminates N × (4 div + 4 cmp) per scan |
| OLA two-range split | Wrap-aware accumulate/read without per-sample modulo | Removes N branch+mod in hot output path |
