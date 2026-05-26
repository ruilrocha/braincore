#pragma once

#include "BlockContext.h"

namespace audio::usecase {

/**
 * A single stage in the audio block pipeline.
 *
 * Stages are called in order by AudioPipeline::run().  Each stage reads from
 * and writes into the shared BlockContext.  Stages are stateful (e.g.
 * SynthesisStage tracks the previous block for spectral morph) but are NOT
 * thread-safe — each pipeline instance must be owned by a single thread.
 */
class IBlockStage {
public:
    virtual ~IBlockStage() = default;

    /**
     * Process one block.
     *
     * @param ctx  Mutable context carrying all inter-stage data for this block.
     *             The stage reads from fields populated by prior stages and
     *             writes its own outputs.
     */
    virtual void process(BlockContext& ctx) = 0;
};

}  // namespace audio::usecase
