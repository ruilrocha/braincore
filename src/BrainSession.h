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
 * ## Brain config (call before addSamples / buildIndex)
 *   setBlockSize()    — samples per block (affects resolution vs latency)
 *   setWindowShape()  — windowing before MFCC analysis
 *   setNumSynapses()  — K-NN neighbours per block (Synaptic strategy quality)
 *
 * ## Lifecycle
 *   setBlockSize / setWindowShape / setNumSynapses  → must call addSamples + buildIndex again
 *   setSearchStrategy                               → reprocess target only (brain stays)
 */
class BrainSession {
public:
    BrainSession();
    ~BrainSession();

    BrainSession(BrainSession&&) noexcept;
    BrainSession& operator=(BrainSession&&) noexcept;
    BrainSession(const BrainSession&) = delete;
    BrainSession& operator=(const BrainSession&) = delete;

    // ── Brain config (requires re-prepare) ───────────────────────────

    void setBlockSize(int block_size) const noexcept;
    [[nodiscard]] int getBlockSize() const noexcept;

    void setWindowShape(WindowShape shape) const noexcept;
    [[nodiscard]] WindowShape getWindowShape() const noexcept;

    /** K nearest neighbours stored per block. Only used by Synaptic strategy. */
    void setNumSynapses(std::size_t n) const noexcept;
    [[nodiscard]] std::size_t getNumSynapses() const noexcept;

    /** Clear all ingested sounds and reset the Brain, preserving config settings. */
    void clear() const noexcept;

    // ── Search strategy (reprocess target only) ───────────────────────

    void setSearchStrategy(SearchStrategy strategy) const;
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
