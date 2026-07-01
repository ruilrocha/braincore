#pragma once

#include "Block.h"
#include "BlockConfig.h"
#include "NearestNeighbourIndex.h"
#include "Sound.h"
#include "VideoSegment.h"
#include "constants.h"
#include "port/IAnalyser.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace audio {

/**
 * Metadata for a source sound loaded into the brain.
 * Tracks which blocks belong to it and whether it is enabled.
 */
struct SourceSound {
    std::string filename;
    std::size_t start = 0;  ///< First block index (inclusive).
    std::size_t end = 0;    ///< Last block index (exclusive).
    std::size_t num_blocks = 0;
    bool enabled = true;
};

/**
 * Core domain aggregate: an immutable collection of fingerprinted audio blocks.
 *
 * Brain is a pure data container after addSound() calls complete.
 * It knows nothing about search strategies or per-stream state
 * (current index, usage counters) — those concerns belong to PlayHead and
 * the use-case layer.
 *
 * An optional nearest-neighbour index can be built once by calling buildIndex()
 * before any PlayHead is created.  The index owns both a VP tree (for O(log N)
 * dynamic queries) and a precomputed K-NN table (the "synapses") — a single
 * unified structure that any search strategy can consult via `index()`.
 *
 * Typical lifecycle:
 * @code
 *   auto brain = std::make_shared<Brain>(analyser, config);
 *   brain->addSound(sound1, "a.wav");
 *   brain->addSound(sound2, "b.wav");
 *   brain->buildIndex();          // optional; required for SynapticSearch
 *   // brain is now fully constructed and safe to share as const.
 * @endcode
 *
 * For a cheap strategy-only rebuild (no re-fingerprinting), use rebuild():
 * @code
 *   auto new_brain = Brain::rebuild(old_brain->blocks(), analyser, new_config);
 * @endcode
 */
class Brain {
public:
    /**
     * @param analyser  Fingerprint strategy (injected port).
     * @param config    Block segmentation configuration (size, overlap, window).
     */
    explicit Brain(std::shared_ptr<port::IAnalyser> analyser, const BlockConfig& config = {});

    // ── Ingestion (call before any audio thread uses this Brain) ────────

    /** Segment @p sound into blocks, fingerprint each, and store them.
     *
     *  @param sound  Audio data to ingest.
     *  @param name   Label / path for source tracking.
     *  @param video  Optional video metadata. When provided, each block
     *                receives a VideoSegment with the computed time offset.
     */
    void addSound(const Sound& sound, const std::string& name = "",
                  const std::optional<VideoMetadata>& video = std::nullopt);

    // ── Index build ─────────────────────────────────────────────────────

    /**
     * Build the nearest-neighbour index (VP tree + precomputed K-NN table).
     *
     * Call once, after all addSound() calls and before any PlayHead is created.
     * Reads are thread-safe after this call returns.
     *
     * The index unifies two roles:
     * - `index()->neighbors(i)` → O(1) precomputed K nearest neighbours for
     *   block i, used by SynapticSearch.
     * - `index()->kNearest(fp, k)` → O(log N) dynamic exact query for an
     *   arbitrary fingerprint, used by VpTreeSearch.
     *
     * @param num_synapses  K for the precomputed K-NN table.
     */
    void buildIndex(std::size_t num_synapses = kDefaultNumSynapses);

    // ── Cheap rebuild factory ───────────────────────────────────────────

    /**
     * Build a new Brain from pre-fingerprinted blocks without re-analysis.
     *
     * Use when only the search strategy or synapse graph needs to change —
     * this avoids the O(N × FFT) cost of a full reconstruction.
     * The @p blocks are *copied*, so the source Brain remains valid and can
     * continue to be used by an in-flight audio thread.
     *
     * @param blocks   Pre-fingerprinted blocks (copied into the new Brain).
     * @param analyser Analyser to associate with the new Brain.
     * @param config   Block configuration for the new Brain.
     */
    static std::shared_ptr<Brain> rebuild(const std::vector<Block>& blocks,
                                          std::shared_ptr<port::IAnalyser> analyser,
                                          const BlockConfig& config);

    // ── Source management ───────────────────────────────────────────────

    /** Enable or disable a source sound by filename. */
    void activateSound(const std::string& filename, bool active);

    /**
     * Check whether the block at @p index belongs to an enabled source.
     * O(1) — uses a precomputed active-flag vector.
     */
    [[nodiscard]] bool isBlockActive(std::size_t index) const;

    /** Access loaded source metadata. */
    [[nodiscard]] const std::vector<SourceSound>& sources() const { return sources_; }

    // ── Const data accessors (safe for concurrent audio-thread reads) ───

    [[nodiscard]] std::size_t size() const { return blocks_.size(); }
    [[nodiscard]] bool empty() const { return blocks_.empty(); }

    [[nodiscard]] const BlockConfig& blockConfig() const { return config_; }
    [[nodiscard]] int blockSize() const { return config_.block_size; }

    [[nodiscard]] const port::IAnalyser& analyser() const { return *analyser_; }
    [[nodiscard]] const std::vector<Block>& blocks() const { return blocks_; }

    /**
     * Return a span into the contiguous mel row for block @p i.
     *
     * The matrix is flat, row-major (N × mel_dim), which allows O(N) distance
     * scans to iterate sequentially through memory — no per-block pointer chase.
     * Returns an empty span if @p i is out of range or no blocks have been loaded.
     */
    [[nodiscard]] std::span<const float> melRow(std::size_t i) const noexcept {
        if (mel_dim_ == 0 || i >= blocks_.size()) {
            return {};
        }
        return {mel_matrix_.data() + i * mel_dim_, mel_dim_};
    }
    [[nodiscard]] std::size_t melDim() const noexcept { return mel_dim_; }

    // ── Index accessors ─────────────────────────────────────────────────

    /** Returns true if buildIndex() has been called. */
    [[nodiscard]] bool hasIndex() const noexcept { return index_.has_value(); }

    /**
     * Return up to @p k nearest neighbours of @p fingerprint in the index.
     *
     * Requires buildIndex() to have been called; throws std::runtime_error if
     * the index is absent.
     */
    [[nodiscard]] std::vector<std::size_t> kNearest(const std::vector<float>& fingerprint,
                                                    std::size_t k) const;

    /**
     * Return the precomputed K nearest neighbours of block @p block_index (O(1)).
     *
     * Returns an empty span if the index is absent or @p block_index is out of
     * range.  The returned span is valid for the lifetime of this Brain.
     */
    [[nodiscard]] std::span<const std::size_t> neighbors(std::size_t block_index) const;

private:
    std::shared_ptr<port::IAnalyser> analyser_;
    BlockConfig config_;
    std::vector<Block> blocks_;
    std::vector<SourceSound> sources_;
    std::vector<bool>
        block_active_;  ///< Precomputed: true iff blocks_[i] belongs to an enabled source.

    // Flat row-major mel matrix: blocks_.size() × mel_dim_ floats.
    // Cache-friendly for O(N) distance scans — all mel vectors contiguous.
    std::vector<float> mel_matrix_;
    std::size_t mel_dim_ = 0;

    std::optional<NearestNeighbourIndex> index_;

    void rebuildActiveFlags();
};

}  // namespace audio
