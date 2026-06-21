#pragma once

#include "../domain/Sound.h"
#include "../domain/WindowShape.h"
#include "BrainEngineTypes.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace audio {

/**
 * Reusable C++ audio processing engine.
 *
 * Encapsulates the complete Brain → PlayHead → OLA → SpectralMorph pipeline
 * with a pure C++ API.  Intended to be consumed directly by any C++ target
 * (CMake package, CLI app, test harness) as well as wrapped by BrainSession
 * for Swift interop.
 *
 * ## Lifecycle
 *   1. Configure: setBlockSize / setOverlapRatio / setWindowShape / setNumSynapses
 *   2. Ingest:    addSound() one or more times
 *   3. Index:     buildIndex()
 *   4. Play:      advance() or advanceInfinite() in a loop
 *   5. Output:    getBlockSamplesInterleaved() after each advance
 *   6. Reset:     clear() to remove all sounds but keep config
 *
 * ## OLA (Overlap-Add)
 *   When overlapRatio > 0, consecutive matched blocks are windowed and
 *   overlap-summed.  stepSize() returns the number of output samples per
 *   advance() call and is the correct chunk size for both input and output.
 *
 * ## Thread safety
 *   Search parameters (setUsageWeight, setMfccWeight, …) use relaxed atomics
 *   so they can be written from a UI thread while advance() runs on an audio
 *   thread.  A stale-by-one-block read is acceptable for all param fields.
 *   All other methods are NOT thread-safe and must be called from one thread.
 */
class BrainEngine {
public:
    BrainEngine();
    ~BrainEngine();

    BrainEngine(BrainEngine&&) noexcept;
    BrainEngine& operator=(BrainEngine&&) noexcept;
    BrainEngine(const BrainEngine&) = delete;
    BrainEngine& operator=(const BrainEngine&) = delete;

    // ── Brain config (requires rebuild: clear + addSound + buildIndex) ────

    void setBlockSize(int block_size) noexcept;
    [[nodiscard]] int getBlockSize() const noexcept;

    /**
     * OLA synthesis overlap ratio [0.0, 0.9].
     * 0.0 = no OLA (hard cuts); 0.5 = 50% crossfade (default).
     * Hann window at 50% gives perfect reconstruction.
     * Requires rebuild to take effect.
     */
    void setOverlapRatio(double ratio) noexcept;
    [[nodiscard]] double getOverlapRatio() const noexcept;

    /**
     * OLA synthesis window shape.
     * Controls per-block envelope during overlap-add; not used for MFCC analysis.
     * Requires rebuild to take effect.
     */
    void setWindowShape(WindowShape shape) noexcept;
    [[nodiscard]] WindowShape getWindowShape() const noexcept;

    /** K nearest neighbours per block (Synaptic/Markov strategies). */
    void setNumSynapses(std::size_t n) noexcept;
    [[nodiscard]] std::size_t getNumSynapses() const noexcept;

    // ── Search strategy (immediate — no rebuild needed) ───────────────────

    void setSearchStrategy(SearchStrategy strategy);
    [[nodiscard]] SearchStrategy searchStrategy() const noexcept;

    // ── Search params (thread-safe, relaxed atomics) ───────────────────────

    /** "Novelty" — re-use penalty [0, 1]. 0 = blocks repeat freely. */
    void setUsageWeight(double value) noexcept;
    /** "Boredom" — usage decay rate [0, 1]. 1.0 = no decay. */
    void setUsageFalloff(double value) noexcept;
    /** Stickyness [0, 1] — temporal coherence bias toward next sequential block. */
    void setStickyness(double value) noexcept;
    /** MFCC timbral weight [0, 1] for multi-feature distance. */
    void setMfccWeight(double value) noexcept;
    /** Mel filter-bank envelope weight [0, 1]. */
    void setMelWeight(double value) noexcept;
    /** FFT spectral magnitude weight [0, 1]. */
    void setSpectralWeight(double value) noexcept;
    /** Raw-vs-normalised fingerprint blend [0=raw, 1=normalised]. */
    void setNRatio(double value) noexcept;
    /** Spectral brightness target [0=bass, 0.5=neutral, 1=treble] — bias block selection by
     * frequency content. */
    void setBrightnessTarget(double value) noexcept;
    /** Brightness bias strength [0, 1] — 0=off, 0.5=strong preference, 1=near-pure brightness
     * selection. Uses exponential penalty (e^(weight × deviation² × 10)) so the effect is
     * perceptible across any brain. */
    void setBrightnessWeight(double value) noexcept;

    // ── Effects ───────────────────────────────────────────────────────────

    /** Add an effect to the pipeline (no-op if already present). */
    void addEffect(EffectType type);
    /** Remove an effect from the pipeline (no-op if absent). */
    void removeEffect(EffectType type);
    /** Set the mix amount for an effect [0.0, 1.0]. */
    void setEffectAmount(EffectType type, double amount) noexcept;

    // ── Ingestion ─────────────────────────────────────────────────────────

    /**
     * Segment @p sound into blocks, fingerprint each, and store them.
     * @param sound  Audio data (sample rate is used for time-offset metadata).
     * @param name   Optional source label (e.g. filename) for metadata queries.
     */
    void addSound(const Sound& sound, const std::string& name = "");

    /** Build the nearest-neighbour index (VP tree + K-NN table). Call once. */
    void buildIndex();

    /** Remove all ingested sounds and reset playback state. Preserves config. */
    void clear() noexcept;

    /** True if the Brain has been initialised (after the first addSound call). */
    [[nodiscard]] bool hasBrain() const noexcept;

    // ── Playback ──────────────────────────────────────────────────────────

    /**
     * Advance one block using a target audio chunk.
     * @param samples     Mono target samples (length should equal stepSize()).
     * @param sample_rate Sample rate of the target chunk.
     * @return            Index of the matched source block.
     */
    [[nodiscard]] std::size_t advance(const std::vector<double>& samples, int sample_rate);

    /**
     * Generate one block of infinite (target-free) audio.
     * Walks timbral space via drift from previous matched block.
     * @return Index of the matched source block.
     */
    [[nodiscard]] std::size_t advanceInfinite(int sample_rate);

    // ── Block data ────────────────────────────────────────────────────────

    [[nodiscard]] std::size_t blockCount() const noexcept;
    [[nodiscard]] std::size_t blockSize() const noexcept;

    /**
     * Frames per advance() step.
     * = blockSize() when OLA inactive; = blockSize() * (1 − overlap) when active.
     */
    [[nodiscard]] std::size_t stepSize() const noexcept;
    [[nodiscard]] int blockChannels(std::size_t index) const noexcept;

    /**
     * Copy mono samples for block @p index into @p out_buffer.
     * Applies the effect chain if active.
     * @return Number of samples written.
     */
    [[nodiscard]] std::size_t getBlockSamples(std::size_t index, double* out_buffer,
                                              std::size_t max_count) const;

    /**
     * Copy interleaved multi-channel output for the last matched block.
     * Uses OLA output when active; applies effect chain if active.
     * @return Number of frames written.
     */
    [[nodiscard]] std::size_t getBlockSamplesInterleaved(std::size_t index, double* out_buffer,
                                                         std::size_t max_frames) const;

    // ── Block source metadata ─────────────────────────────────────────────

    /** Source name/label for block @p index. Empty string if out of range. */
    [[nodiscard]] std::string getBlockSourceName(std::size_t index) const;

    /**
     * Time offset (seconds) of block @p index within its source.
     * Returns -1.0 if out of range.
     */
    [[nodiscard]] double getBlockTimeOffset(std::size_t index) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audio
