# Core Concepts

## Brain

The central aggregate. Holds a collection of audio `Block`s extracted from source files.
Given a target block, it finds the best match using a pluggable search strategy.

```
Source files → Brain::addSound() → Segmented + analysed → Blocks indexed in VP-tree
Target block → Brain::findBestMatch() → Best-matching Block
```

## Block

A fixed-length audio segment (default 4096 samples ≈ 93ms at 44.1kHz):

```
Block {
    channel_samples: [[Double]]   // per-channel raw audio (ch 0 = fingerprint source)
    analysis: BlockAnalysis       // fingerprint pair (raw + normalised)
    source_name: String           // originating file
}
```

## AudioPrint / BlockAnalysis

An `AudioPrint` bundles four fingerprint vectors computed from a single FFT pass:

| Field | Size | What it captures |
|-------|------|-----------------|
| `mfcc` | ~12 | Timbral shape (DCT of Mel log energies) |
| `mel` | ~24 | Spectral envelope (Mel filter bank output, pre-DCT) |
| `spectral` | ~100 | Full harmonic detail (FFT magnitude bins) |
| `chroma` | 12 | Pitch-class profile (C…B, L1-normalised, octave-invariant) |

A `BlockAnalysis` pairs two `AudioPrint`s:
- `print` — computed from raw windowed samples
- `normalised_print` — computed from DC-removed, peak-scaled samples

The `n_ratio` parameter blends between them: `0.0` = amplitude-sensitive, `1.0` = amplitude-invariant.

## Search Strategies

Determines which brain block best matches a target block. All implement `ISearchStrategy`
and can be swapped at runtime without interrupting playback.

See [search-strategies.md](./search-strategies.md) for details on each strategy.

## OLA Output Synthesis

After a matching block is selected, `OlaBuffer` performs overlap-add (OLA) synthesis:

- Raw `channel_samples` are windowed with the user-selected window shape
- Windowed blocks are accumulated into a circular buffer with `overlap` fraction of overlap
- `stepSize = blockSize × (1 − overlap)` samples are read out per step
- At 50% overlap with a Hann window, output amplitude is perfectly uniform

This smooths hard cuts between blocks. At 0% overlap it's equivalent to direct block splicing.

## Spectral Morphing

Post-processing effect applied after block selection. Cross-fades consecutive matched blocks
in the frequency domain (FFT magnitude interpolation + phase blending with RMS matching).
Higher `amount` → smoother timbral transitions; at full strength creates a reverb-like smear.

## Usage, Novelty & Boredom

Prevents the brain from repeating the same blocks:

- **novelty** (`usage_weight`) — penalises high-usage blocks in scoring; higher = prefer fresh blocks
- **boredom** (`usage_falloff`) — how fast usage decays; lower = blocks stay "tired" longer

## Stickyness

Biases the search toward the *next sequential block* in the source:
- `0.0` → pure similarity matching (may jump freely between sources)
- `1.0` → strongly prefers sequential playback (more coherent, less creative)

## BlockConfig

Controls how audio is segmented and how output is synthesised:

| Field | Description |
|-------|-------------|
| `block_size` | Samples per block (resolution vs latency) |
| `overlap` | OLA overlap ratio [0, 0.9]; 0.5 recommended |
| `window` | OLA synthesis window shape (Hann, Hamming, Blackman, …) |

Changing any of these requires a full rebuild (clear → addSamples → buildIndex).
