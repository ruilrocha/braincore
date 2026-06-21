#pragma once

#include "Brain.h"
#include "SearchParams.h"
#include "port/ISearchStrategy.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace audio {

/**
 * A mutable cursor that traverses an immutable Brain.
 *
 * PlayHead bundles all per-stream state that changes as audio plays:
 *   - current position in the brain (block index)
 *   - per-block usage counters (for novelty / boredom effects)
 *   - the search strategy instance (may carry its own internal state)
 *
 * The Brain it references is fully immutable after construction and safe to
 * share across any number of concurrent PlayHeads.
 *
 * ## Typical usage
 * @code
 *   PlayHead ph(brain, std::make_shared<ClosestSearch>());
 *   // inside the audio loop:
 *   std::size_t idx = ph.advance(target_fp, params);
 *   const Block& match = ph.brain().blocks()[idx];
 * @endcode
 *
 * ## Thread safety
 * A PlayHead is NOT thread-safe.  Each audio thread must own its own PlayHead.
 * Multiple PlayHeads may safely share the same Brain concurrently because Brain
 * is const after ingestion.
 */
class PlayHead {
public:
    /**
     * @param brain   Immutable data library to traverse.  Must remain alive for
     *                the lifetime of this PlayHead.
     * @param search  Search strategy (ownership transferred; may carry internal
     *                state such as synapse traversal position).
     */
    PlayHead(std::shared_ptr<const Brain> brain, std::shared_ptr<port::ISearchStrategy> search);

    // Non-copyable (usage state and strategy state are move-only semantics).
    PlayHead(const PlayHead&) = delete;
    PlayHead& operator=(const PlayHead&) = delete;
    PlayHead(PlayHead&&) = default;
    PlayHead& operator=(PlayHead&&) = default;

    /**
     * Select the best-matching block for @p target, update internal state.
     *
     * @param target  Raw + normalised fingerprints of the target block.
     * @param params  Live search / blend / effect parameters snapshot.
     * @return        Index into brain().blocks() of the chosen block.
     */
    [[nodiscard]] std::size_t advance(const BlockAnalysis& target, const SearchParams& params);

    /**
     * Reset traversal state: position returns to 0, all usage counters clear.
     * Does NOT reset any strategy-internal state.
     */
    void reset();

    /**
     * Rebind to a new Brain + strategy (e.g. after a UI rebuild).
     *
     * Resets the PlayHead to index 0 and resizes usage counters to match
     * the new Brain.  The new Brain must remain alive for the lifetime of
     * this PlayHead.
     */
    void rebind(std::shared_ptr<const Brain> brain, std::shared_ptr<port::ISearchStrategy> search);

    // ── Accessors ────────────────────────────────────────────────────────

    [[nodiscard]] const Brain& brain() const { return *brain_; }
    [[nodiscard]] std::size_t currentIndex() const { return current_block_idx_; }

    /// Read-only view of usage counters (one entry per brain block).
    [[nodiscard]] const std::vector<double>& blockUsages() const { return block_usages_; }

    /**
     * Multiply all usage counters by @p factor (e.g. 0.995 to decay slowly).
     * Used by infinite mode to force blocks to gradually become available again.
     * Normal usage increment/decay is handled by the strategy via advance().
     */
    void depleteUsages(double factor);

private:
    std::shared_ptr<const Brain> brain_;
    std::shared_ptr<port::ISearchStrategy> search_;
    std::size_t current_block_idx_ = 0;
    std::vector<double> block_usages_;  ///< one entry per brain_.blocks() element
};

}  // namespace audio
