#include "AudioPipeline.h"

namespace audio::usecase {

void AudioPipeline::run(BlockContext& ctx) {
    for (auto& stage : stages_) {
        stage->process(ctx);
    }
}

IBlockStage* AudioPipeline::stage(std::size_t index) {
    if (index >= stages_.size()) {
        return nullptr;
    }
    return stages_[index].get();
}

}  // namespace audio::usecase
