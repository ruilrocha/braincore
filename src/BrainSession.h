#pragma once

#include "domain/WindowShape.h"

#include <cstddef>
#include <memory>
#include <string>

namespace audio {

enum class SearchStrategy : int {
    Closest = 0,
    VpTree = 1,
    Synaptic = 2,
};

/**
 * Available effect types for BrainSession's effect pipeline.
 * Values are stable across versions — safe to store in Swift as Int32.
 */
enum class EffectType : int {
    SpectralMorph = 0,  ///< Spectral morphing between consecutive blocks.
};

/**
 * High-level session facade — safe for Swift C++ interop.
 * All heavy C++ headers are confined to BrainSession.cpp via Pimpl.
 *
 * ## Brain config (call before addSamples / buildIndex — requires rebuild)
 *   setBlockSize()      — samples per block (affects resolution vs latency; default 4096)
 *   setOverlapRatio()   — OLA synthesis overlap [0.0, 0.9]; 0.5 = 50% crossfade (default)
 *   setWindowShape()    — synthesis window shape for OLA output (Hann recommended)
 *   setNumSynapses()    — K-NN neighbours per block (Synaptic strategy quality)
 *
 * ## Window semantics
 *   Analysis (MFCC fingerprinting) always uses a hardcoded Hann window internally.
 *   The user-selected window shape controls OLA synthesis output only — it shapes
 *   how each matched block blends into the next during Overlap-Add reconstruction.
 *
 * ## OLA (Overlap-Add) synthesis
 *   When overlap > 0, consecutive matched blocks are windowed and overlap-summed in
 *   an accumulation buffer, producing smooth crossfades instead of hard cuts.
 *   stepSize() returns the number of output samples per advance() call (= blockSize()
 *   when overlap==0, or blockSize()*(1-overlap) when OLA is active).
 *   OLA is single-threaded: advance() accumulates and getBlockSamplesInterleaved() reads —
 *   both must be called from the same thread.
 *
 * ## Lifecycle
 *   setBlockSize / setOverlapRatio / setWindowShape / setNumSynapses
 *     → store the new value, then call clear() + addSamples() + buildIndex() to rebuild.
 *   setSearchStrategy → switches immediately, no rebuild needed.
 */
class BrainSession {
public:
    BrainSession();
    ~BrainSession();

    BrainSession(BrainSession&&) noexcept;
    BrainSession& operator=(BrainSession&&) noexcept;
    BrainSession(const BrainSession&) = delete;
    BrainSession& operator=(const BrainSession&) = delete;

    // ── Brain config (requires rebuild: clear + addSamples + buildIndex) ─────

    void setBlockSize(int block_size) noexcept;
    [[nodiscard]] int getBlockSize() const noexcept;

    /**
     * OLA synthesis overlap ratio [0.0, 0.9].
     * 0.0 = no OLA (hard cuts between blocks).
     * 0.5 = 50% overlap — consecutive blocks crossfade; Hann window gives perfect reconstruction.
     * Default: 0.5.
     * Requires rebuild (clear + addSamples + buildIndex) to take effect.
     */
    void setOverlapRatio(double ratio) noexcept;
    [[nodiscard]] double getOverlapRatio() const noexcept;

    /**
     * OLA synthesis window shape.
     * Controls how each matched block is enveloped before overlap-adding to the output.
     * Not used for analysis — MFCC fingerprinting always uses Hann internally.
     * Hann at 50% overlap gives perfect reconstruction (default).
     * Requires rebuild to take effect.
     */
    void setWindowShape(WindowShape shape) noexcept;
    [[nodiscard]] WindowShape getWindowShape() const noexcept;

    /** K nearest neighbours stored per block. Only used by Synaptic strategy. */
    void setNumSynapses(std::size_t n) noexcept;
    [[nodiscard]] std::size_t getNumSynapses() const noexcept;

    /** Clear all ingested sounds and reset the Brain, preserving config settings. */
    void clear() noexcept;

    // ── Search strategy (reprocess target only) ───────────────────────

    void setSearchStrategy(SearchStrategy strategy);
    [[nodiscard]] SearchStrategy searchStrategy() const noexcept;

    // ── Ingestion ────────────────────────────────────────────────────

    void addSamples(const double* samples, std::size_t count, int sample_rate, const char* name);

    void addSamplesInterleaved(const double* samples, std::size_t frame_count, int channels,
                               int sample_rate, const char* name);

    void buildIndex();

    // ── Search params (live, take effect on next advance) ─────────────

    /** "Novelty" — re-use penalty. 0 = blocks can repeat freely, 1 = strongly avoid repeats. */
    void setUsageWeight(double v) noexcept;
    /** "Boredom" — usage decay rate per step. 1.0 = no decay (blocks stay avoided), 0.0 = instant
     * reset. */
    void setUsageFalloff(double v) noexcept;
    /** Stickyness [0,1] — bias toward the next sequential block for temporal coherence. */
    void setStickyness(double v) noexcept;
    /** MFCC timbral weight [0,1] for multi-feature distance. */
    void setMfccWeight(double v) noexcept;
    /** Mel filter-bank envelope weight [0,1]. */
    void setMelWeight(double v) noexcept;
    /** FFT spectral magnitude weight [0,1]. */
    void setSpectralWeight(double v) noexcept;
    /** Raw-vs-normalised fingerprint blend [0=raw, 1=normalised]. */
    void setNRatio(double v) noexcept;

    // ── Post-processing effects ────────────────────────────────────────

    /** Add an effect to the pipeline (no-op if already present). */
    void addEffect(EffectType type);
    /** Remove an effect from the pipeline (no-op if absent). */
    void removeEffect(EffectType type);
    /** Set the mix amount for an effect [0.0, 1.0]. Applied on the next getBlockSamples call. */
    void setEffectAmount(EffectType type, double amount) noexcept;

    /**
     * Advance one block using a target chunk (normal streaming mode).
     * Uses stored SearchParams; depletes usage each step.
     * @return Index of the matched source block.
     */
    std::size_t advance(const double* samples, std::size_t count, int sample_rate);

    /**
     * Generate one block of infinite (target-free) audio.
     *
     * Starts from a random noise fingerprint on the first call, then evolves
     * via a drift walk toward whatever the brain finds most similar.
     * Respects stored SearchParams (usage_weight, usage_falloff, stickyness).
     *
     * @return Index of the matched source block.
     */
    std::size_t advanceInfinite(int sample_rate);

    // ── Block data ────────────────────────────────────────────────────

    [[nodiscard]] std::size_t blockCount() const noexcept;
    [[nodiscard]] std::size_t blockSize() const noexcept;

    /**
     * Output samples (frames) per advance() step.
     *
     * When overlap == 0 (OLA inactive): equals blockSize().
     * When overlap > 0 (OLA active): equals blockSize() * (1 − overlap).
     *
     * Use this as the chunk size for target audio passed to advance() AND as
     * the expected frame count returned by getBlockSamplesInterleaved().
     */
    [[nodiscard]] std::size_t stepSize() const noexcept;
    [[nodiscard]] int blockChannels(std::size_t index) const noexcept;

    std::size_t getBlockSamples(std::size_t index, double* out_buffer,
                                std::size_t max_count) const noexcept;

    std::size_t getBlockSamplesInterleaved(std::size_t index, double* out_buffer,
                                           std::size_t max_frames) const noexcept;

    // ── Block source metadata (for video mapping in Swift) ─────────────

    /** Source filename / label for block at @p index. Empty string if out of range. */
    [[nodiscard]] std::string getBlockSourceName(std::size_t index) const noexcept;

    /**
     * Time offset in seconds of block @p index within its source file.
     * Computed as (block_position_in_source * block_size) / sample_rate.
     * Returns -1.0 if out of range.
     */
    [[nodiscard]] double getBlockTimeOffset(std::size_t index) const noexcept;

    [[nodiscard]] std::string selfTest() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audio
