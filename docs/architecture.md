# Architecture

brain-io follows a **hexagonal (ports & adapters) architecture**. The domain core has zero
external dependencies — it communicates with the outside world only through port interfaces.

## Layers

```
┌─────────────────────────────────────────────┐
│         BrainSession (Swift-facing C++ API) │
└─────────────────────┬───────────────────────┘
                      │
         ┌────────────┼────────────┐
         ▼            ▼            ▼
   ┌──────────┐  ┌──────────┐  ┌──────────────┐
   │ Adapters │  │Use-Cases │  │ Domain Core  │
   │          │  │          │  │              │
   │MfccAnalys│  │BrainSess.│  │ Brain        │
   │PocketfftB│  │(thin     │  │ Block        │
   │OlaBuffer │  │ facade)  │  │ BlockAnalysis│
   │SpectralM.│  │          │  │ AudioPrint   │
   │VpTreeSrch│  └──────────┘  │ PlayHead     │
   │ClosestSrch│               │ NearestNeigh.│
   │SynapticSr│               │ SearchContext │
   │MarkovSrch│               │ BlockConfig  │
   │MomentumSr│               │ SearchParams │
   │          │               │ Ports:       │
   └──────────┘               │  IAnalyser   │
                               │  ISearchStrat│
                               │  ISoundFileGW│
                               │  IAudioOutput│
                               │  IBlockEffect│
                               └──────────────┘
```

## Dependency Rules

```
Domain  ←──  Adapters  ←──  BrainSession  ←──  Swift app
```

- **Domain** (`src/domain/`) — zero external deps; port interfaces live here
- **Adapters** (`src/adapter/`) — implement ports; may use external libraries
- **BrainSession** (`src/BrainSession.h/.cpp`) — thin Pimpl facade; the only C++ surface the Swift app touches

## Key Domain Types

| Type | Role |
|------|------|
| `Brain` | Holds all blocks; answers nearest-neighbour queries via `NearestNeighbourIndex` |
| `Block` | Fixed-length audio segment: `channel_samples` + `analysis: BlockAnalysis` |
| `BlockAnalysis` | `{print, normalised_print}` — both `AudioPrint` variants for a block |
| `AudioPrint` | `{mfcc, mel, spectral, chroma, dominant_freq}` — four fingerprint vectors |
| `PlayHead` | Per-stream mutable cursor: current block index, usage counters, search strategy |
| `SearchContext` | Snapshot passed to `ISearchStrategy::search()` each block |
| `NearestNeighbourIndex` | VP-tree index over all block fingerprints |
| `BlockConfig` | `{block_size, overlap, window}` — segmentation + OLA config |
| `SearchParams` | All realtime-tweakable parameters (stickyness, novelty, weights, …) |

## Data Flow

```
Source audio ──→ Brain::addSound()
                      │
                      ▼
               MfccAnalyser::analyse()  (analysis window: always Hann)
               ├── PocketfftBackend FFT
               ├── MelFilterBank → mel print
               ├── DCT → mfcc print
               ├── FFT magnitude bins → spectral print
               └── Chroma rebinning → chroma print
                      │
                      ▼
               Block { channel_samples, analysis: BlockAnalysis }
                      │
                      ▼
               NearestNeighbourIndex::build()  (VP-tree)

Target audio ──→ Per step:
               1. MfccAnalyser::analyse() on target block
               2. NearestNeighbourIndex::search() → best Block
               3. OlaBuffer::accumulate(block.channel_samples, OLA window)
               4. OlaBuffer::read() → output step
               5. SpectralMorph::apply() if effect active
```

## OLA Output Synthesis

`OlaBuffer` (`src/adapter/effects/`) implements overlap-add synthesis:

- Analysis uses a Hann window (hardcoded) for clean MFCC fingerprints
- **OLA window is user-selected** (`BlockConfig.window`) — applied to raw `channel_samples` during output
- At 50% overlap with Hann window: perfect reconstruction (no amplitude seams)
- `stepSize = blockSize × (1 − overlapRatio)` — advance and retrieve in steps, not full blocks
