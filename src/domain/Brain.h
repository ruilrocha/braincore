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
 *   brain->buildIndex();          // optional; required for SynapticSearch/MarkovChainSearch
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
    explicit Brain(std::shared_ptr<port::IAnalyser> analyser, BlockConfig config = {});

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
     *   block i, used by SynapticSearch and MarkovChainSearch.
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
                                          BlockConfig config);

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
    [[nodiscard]] int overlap() const { return config_.overlap; }

    [[nodiscard]] const port::IAnalyser& analyser() const { return *analyser_; }
    [[nodiscard]] const std::vector<Block>& blocks() const { return blocks_; }

    // ── Index accessors ─────────────────────────────────────────────────

    /**
     * Returns the nearest-neighbour index, or nullptr if buildIndex() has not
     * been called.
     *
     * The index provides both O(1) precomputed neighbourhood access
     * (`index()->neighbors(i)`) and O(log N) dynamic queries
     * (`index()->kNearest(fp, k)`).
     */
    [[nodiscard]] const NearestNeighbourIndex* index() const {
        return index_.has_value() ? &index_.value() : nullptr;
    }

private:
    std::shared_ptr<port::IAnalyser> analyser_;
    BlockConfig config_;
    std::vector<Block> blocks_;
    std::vector<SourceSound> sources_;
    std::vector<bool>
        block_active_;  ///< Precomputed: true iff blocks_[i] belongs to an enabled source.

    std::optional<NearestNeighbourIndex> index_;

    void rebuildActiveFlags();
};

}  // namespace audio
