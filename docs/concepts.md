# Core Concepts

## Brain

The **Brain** is the central aggregate. It holds a collection of audio **Blocks** extracted
from one or more source sounds. When given a target audio block, it finds the best-matching
source block using a pluggable search strategy.

Think of it as a database of audio fragments that can be queried by sonic similarity.

```
Source files → Brain::addSound() → Segmented, analysed, stored as Blocks
Target block → Brain::findBestMatch() → Best-matching Block returned
```

## Block

A **Block** is a fixed-length segment of audio (default: 4096 samples ≈ 93ms at 44.1kHz).
Each block stores:

- **samples**: Mono audio data (channel 0, used for analysis)
- **channel_samples**: Per-channel audio (for stereo/multichannel reconstruction)
- **mfcc**: MFCC audio print (timbral shape)
- **spectral**: FFT magnitude bins (spectral detail)
- **normalised variants**: Same prints computed from amplitude-normalised samples
- **usage**: Selection counter (for novelty/boredom mechanics)
- **synapses**: Pre-computed list of most-similar block indices

## Audio Print

An **Audio Print** is a numeric vector that captures a specific characteristic of an audio
block. brain-io uses two complementary prints:

### MFCC (primary print)

**Mel-Frequency Cepstral Coefficients** capture the *timbral envelope* of a sound —
what it "sounds like" regardless of exact pitch or volume.

Computation: `audio → FFT → Mel filter bank → log energy → DCT → MFCC coefficients`

This is the same technique used in speech recognition to identify phonemes.
For brain-io, it means two blocks will match if they have similar tonal colour
(e.g., both are bright drum hits, or both are dark pads).

### Spectral (secondary print)

The **FFT magnitude spectrum** (downsampled to N bins) captures finer harmonic detail.
This is blended with MFCC via the `blend_ratio` parameter to control matching precision.

- `blend_ratio = 1.0` → Match by timbre only (MFCC)
- `blend_ratio = 0.0` → Match by exact spectrum (FFT bins)
- Between → Blended scoring

### Normalised Variants

Both prints have normalised versions computed from amplitude-normalised audio (DC removed,
peak-scaled to 1.0). The `n_ratio` parameter blends between raw and normalised matching:

- `n_ratio = 0.0` → Amplitude-dependent matching (quiet blocks differ from loud ones)
- `n_ratio = 1.0` → Amplitude-invariant matching (only shape matters)

## Search Strategy

The algorithm that decides which brain block best matches a target block. All strategies
implement the same interface (`ISearchStrategy::search()`) and can be swapped at runtime
**without interrupting playback**. The StreamProcessor snapshots the active strategy on
every block, so changes take effect on the very next block.

Strategies that require synapses (Synaptic, Markov) trigger a lazy build of the synapse
graph on first use — no manual pre-computation needed.

See [search-strategies.md](./search-strategies.md) for detailed descriptions.

## Granular Scatter vs Block-Chopping

These are two **completely different systems** operating at different scales:

### Block-Chopping (Core Mechanism)

The fundamental unit of brain-io. Audio is segmented into fixed-length blocks (e.g., 4096
samples) for analysis and matching. This is **not an effect** — it's the structural basis
of how the program works. Without blocks, there's nothing to match.

### Granular Scatter (Post-Processing Effect)

After a block is selected as the match, granular scatter optionally re-slices it into
**micro-grains** (tiny sub-segments, often 10-100ms) with randomised parameters:

- **scatter**: Random temporal offset per grain
- **density**: How many grains overlap
- **pitch_jitter**: Random playback speed per grain
- **amp_variation**: Random volume per grain

This creates rich, cloud-like textures from what would otherwise be a simple block splice.
It's applied *after* matching, as a textural enhancement.

**In short**: Block-chopping decides WHICH audio plays. Granular scatter changes HOW it sounds.

## Synapses

A pre-computed similarity graph over all blocks. For each block, the N most similar blocks
(by audio print distance) are stored. This enables graph-walk search strategies
(SynapticSearch, MarkovChainSearch) that traverse the timbral space by stepping between
neighbours rather than scanning all blocks every time.

Built via `Brain::buildSynapses()`. Computed lazily when a synapse-based strategy is first
selected at runtime.

## Usage, Novelty & Boredom

A mechanism to prevent the brain from getting stuck on the same few blocks:

- **usage**: Counter incremented each time a block is selected
- **novelty** (`usage_weight`): How much to penalise high-usage blocks in scoring.
  High novelty = prefer fresh blocks, even if they're less similar.
- **boredom** (`usage_falloff`): How fast usage decays over time.
  Low boredom = blocks stay "tired" longer.

## Stickyness

Temporal coherence bias. When `stickyness > 0`, the search is biased toward choosing the
*next sequential block* in the source (i.e., the block that originally followed the
current one). This produces smoother, more coherent output at the cost of less creative
matching.

- `stickyness = 0.0` → Pure similarity matching (may jump wildly between sources)
- `stickyness = 1.0` → Strongly prefers sequential playback

## Spectral Morphing

A post-processing effect that cross-fades between consecutive matched blocks in the
frequency domain (FFT magnitude interpolation + phase blending with RMS matching).
Produces smooth timbral transitions instead of hard cuts between blocks. Controlled
by the `spectral_morph` parameter (0.0 = no morphing, 1.0 = full cross-fade).

## Mel Filter Bank

Triangular filters in the Mel (perceptual) frequency scale, used to convert an FFT
magnitude spectrum into perceptual energy bands. These bands feed the DCT to produce
MFCC coefficients. Stored as a sparse weight matrix — each filter only contains
non-zero weights for bins within its triangle, and the magnitude spectrum is computed
once (not per-filter).

## Modes

| Mode | Description |
|------|-------------|
| **Batch** | Process entire target offline, save result to file |
| **Stream** | Loop target continuously, real-time playback via IAudioOutput |
| **Infinite** | No target — generate endless evolving soundscapes by drifting through timbral space |
| **UI** | Interactive mode with WebSocket control panel for live parameter tweaking |

## Video Support (`BRAINIO_BUILD_VIDEO`)

brain-io can ingest video files as brain sources. The audio track is extracted and
fed into the matching pipeline exactly like any other audio source — the similarity
engine is unchanged.

**VideoSegment** is a value object attached to each `Block` that originated from a
video file. It stores the source video path, the time offset (seconds) where this
block's audio starts, and the block duration. Blocks from audio-only sources carry no
`VideoSegment` (represented as `std::nullopt`).

**VideoMetadata** is the input you pass to `Brain::addSound()` when loading a video
source: the file path and where in the video the audio starts. The brain stamps each
block with a computed `VideoSegment` offset at load time — no computation during
search.

**IVideoSource** (port) reads video files: extracts the audio track, queries file
metadata (dimensions, fps, duration), and decodes a single RGB24 frame at a given
timestamp.

**IVideoOutput** (port) consumes output: called once per matched block with either
the block's `VideoSegment` (video source) or `nullopt` (audio-only source → black
frame). The Swift/iOS implementation uses AVFoundation to seek the player natively;
the CLI implementation uses FFmpeg to write a remixed video file.

**FfmpegVideoSource** caches up to 4 simultaneously open `AVFormatContext` +
`AVCodecContext` handles per path (LRU eviction), so repeated `readFrame()` calls
on the same file do not pay file-open overhead. The audio track is resampled to 44100
Hz via `SwrContext`.

**FfmpegVideoOutput** writes an H.264/MP4 file — one frame per matched audio block.
The output file path is derived from the audio output path (same name, `.mp4`
extension). Opened lazily on the first `onBlock()` call.

**Building with video support** requires ffmpeg to be installed separately (it is not
in `conandata.yml` as building from source is heavy):

```sh
conan install --requires="ffmpeg/7.1.1" --output-folder=cmake-build-debug/conan/build/Debug --build=missing
cmake -DBRAINIO_BUILD_VIDEO=ON ...
```

**CLI usage:**
```sh
./brainio -v myvideo.mp4 -t sounds/target.wav
```
