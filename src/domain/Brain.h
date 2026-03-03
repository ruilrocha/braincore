#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Block.h"
#include "BlockConfig.h"
#include "Sound.h"
#include "SearchParams.h"
#include "port/IAnalyser.h"
#include "port/ISearchStrategy.h"

namespace audio {

/**
 * Metadata for a source sound loaded into the brain.
 * Tracks which blocks belong to it and whether it is enabled.
 */
struct SourceSound {
    std::string  filename;
    std::size_t  start      = 0;   ///< First block index (inclusive).
    std::size_t  end        = 0;   ///< Last block index (exclusive).
    std::size_t  num_blocks = 0;
    bool         enabled    = true;
};

/**
 * Core domain aggregate: a collection of fingerprinted audio blocks.
 *
 * The Brain knows nothing about any concrete analysis technique or
 * external library.  It interacts with the outside world exclusively
 * through the port interfaces IAnalyser and ISearchStrategy.
 */
class Brain {
public:
    /**
     * @param analyser     Fingerprint strategy (injected port).
     * @param search       Block-selection strategy (injected port).
     * @param config       Block segmentation configuration (size, overlap, window).
     */
    Brain(std::shared_ptr<port::IAnalyser>       analyser,
          std::shared_ptr<port::ISearchStrategy>  search,
          BlockConfig config = {});

    // ── Ingestion ──────────────────────────────────────────────────────

    /** Segment @p sound into blocks, fingerprint each, and store them. */
    void addSound(const Sound& sound, const std::string& name = "");

    // ── Source management ──────────────────────────────────────────────

    /** Enable or disable a source sound by filename. */
    void activateSound(const std::string& filename, bool active);

    /** Check whether the block at @p index belongs to an enabled source. */
    [[nodiscard]] bool isBlockActive(std::size_t index) const;

    /** Access loaded source metadata. */
    [[nodiscard]] const std::vector<SourceSound>& sources() const { return sources_; }

    // ── Search ─────────────────────────────────────────────────────────

    /**
     * Find the best-matching block for @p target_fp according to the
     * injected search strategy and the current search parameters.
     *
     * @return Reference to the selected Block.
     */
    [[nodiscard]] const Block& findBestMatch(
        const std::vector<double>& target_fp,
        const SearchParams& params);

    // ── Synapse graph ──────────────────────────────────────────────────

    /**
     * Pre-compute a similarity graph: for every block, store the indices of
     * the @p num_synapses most similar blocks.
     */
    void buildSynapses(std::size_t num_synapses = 1000);

    // ── Jiggle ─────────────────────────────────────────────────────────

    /**
     * Randomise the current block index.
     * Useful for breaking out of synaptic search loops.
     */
    void jiggle();

    // ── Usage depletion ────────────────────────────────────────────────

    /**
     * Deplete all blocks' usage counters by the falloff rate.
     * Centralised here so search strategies don't duplicate this logic.
     */
    void depleteUsage(double falloff);

    // ── Accessors ──────────────────────────────────────────────────────

    [[nodiscard]] std::size_t size()  const { return blocks_.size(); }
    [[nodiscard]] bool        empty() const { return blocks_.empty(); }

    [[nodiscard]] const BlockConfig& blockConfig() const { return config_; }
    [[nodiscard]] int blockSize() const { return config_.block_size; }
    [[nodiscard]] int overlap()   const { return config_.overlap; }

    [[nodiscard]] const port::IAnalyser& analyser() const { return *analyser_; }

    [[nodiscard]] const std::vector<Block>& blocks() const { return blocks_; }
    [[nodiscard]]       std::vector<Block>& blocks()       { return blocks_; }

    [[nodiscard]] std::size_t currentBlockIndex() const { return current_block_index_; }

private:
    std::shared_ptr<port::IAnalyser>      analyser_;
    std::shared_ptr<port::ISearchStrategy> search_;
    BlockConfig     config_;
    std::vector<Block>        blocks_;
    std::vector<SourceSound>  sources_;
    std::size_t               current_block_index_ = 0;
};

} // namespace audio

