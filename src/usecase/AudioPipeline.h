#pragma once

#include "BlockContext.h"
#include "IBlockStage.h"

#include <memory>
#include <vector>

namespace audio::usecase {

/**
 * An ordered sequence of IBlockStage instances that collectively process one
 * audio block.
 *
 * The pipeline owns its stages.  On each call to run(), every stage's
 * process() is called in insertion order, sharing the same BlockContext.
 *
 * ## Usage
 * @code
 *   AudioPipeline pipeline;
 *   pipeline.addStage(std::make_unique<AnalysisStage>(...));
 *   pipeline.addStage(std::make_unique<SearchStage>(std::move(playhead)));
 *   pipeline.addStage(std::make_unique<SynthesisStage>(...));
 *   pipeline.addStage(std::make_unique<OutputStage>(...));
 *
 *   BlockContext ctx;
 *   ctx.block_index = block_idx;
 *   ctx.params = active_params;
 *   ctx.audio_start_sec = audio_clock;
 *   pipeline.run(ctx);
 * @endcode
 *
 * ## Thread safety
 * AudioPipeline is NOT thread-safe.  The owning thread must call run()
 * exclusively (typically the audio processing thread).
 */
class AudioPipeline {
public:
    /// Add a stage at the end of the pipeline.  Must be called before run().
    /// Returns a non-owning pointer to the just-added stage so callers can
    /// hold a typed observer without taking the pointer before the move.
    template <typename T>
    T* addStage(std::unique_ptr<T> stage) {
        auto* ptr = stage.get();
        stages_.push_back(std::move(stage));
        return ptr;
    }

    /// Run all stages in order on @p ctx.
    void run(BlockContext& ctx);

    /// Access a stage by index (e.g. to cast to a concrete type for setup).
    [[nodiscard]] IBlockStage* stage(std::size_t index);
    [[nodiscard]] std::size_t numStages() const { return stages_.size(); }

private:
    std::vector<std::unique_ptr<IBlockStage>> stages_;
};

}  // namespace audio::usecase
