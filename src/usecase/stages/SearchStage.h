#pragma once

#include "../../domain/PlayHead.h"
#include "../IBlockStage.h"

namespace audio::usecase::stages {

/**
 * Pipeline stage 2: Search.
 *
 * Calls PlayHead::advance() with ctx.fingerprint and ctx.params, then
 * populates ctx.match_idx and ctx.match.
 *
 * Owns the PlayHead — it is the single source of per-stream traversal state
 * (current index, block usages, search strategy).
 */
class SearchStage final : public IBlockStage {
public:
    /**
     * @param playhead  PlayHead to advance.  Ownership transferred.
     */
    explicit SearchStage(PlayHead playhead);

    void process(BlockContext& ctx) override;

    // Access to the owned PlayHead (e.g. for reset / rebind after a UI rebuild).
    [[nodiscard]] PlayHead& playhead() { return playhead_; }
    [[nodiscard]] const PlayHead& playhead() const { return playhead_; }

private:
    PlayHead playhead_;
};

}  // namespace audio::usecase::stages
