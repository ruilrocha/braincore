#include "SearchStage.h"

namespace audio::usecase::stages {

SearchStage::SearchStage(PlayHead playhead) : playhead_(std::move(playhead)) {}

void SearchStage::process(BlockContext& ctx) {
    ctx.match_idx = playhead_.advance(ctx.fingerprint, ctx.params);
    ctx.match = &playhead_.brain().blocks()[ctx.match_idx];
}

}  // namespace audio::usecase::stages
