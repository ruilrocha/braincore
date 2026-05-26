#pragma once

#include "../../domain/BlockConfig.h"
#include "../../domain/Sound.h"
#include "../../domain/port/IAnalyser.h"
#include "../IBlockStage.h"

#include <span>
#include <vector>

namespace audio::usecase::stages {

/**
 * Pipeline stage 1: Analysis.
 *
 * Extracts a windowed slice of the target at ctx.block_index, computes the
 * primary fingerprint, and writes it to ctx.fingerprint.
 *
 * Pre-allocates a working buffer (fp_block_) at construction so no heap
 * allocation occurs on the audio processing thread during run().
 */
class AnalysisStage final : public IBlockStage {
public:
    /**
     * @param target       The target sound (loop-driven externally by the pipeline runner).
     * @param config       Block segmentation config used to slice the target.
     * @param analyser     Fingerprint analyser (ref to Brain's analyser).
     * @param sample_rate  Target sample rate (Hz).
     */
    AnalysisStage(const Sound& target, const BlockConfig& config, const port::IAnalyser& analyser,
                  int sample_rate);

    void process(BlockContext& ctx) override;

private:
    const Sound& target_;
    BlockConfig config_;
    const port::IAnalyser& analyser_;
    int sample_rate_;

    std::size_t step_;

    // Pre-allocated working buffer — avoids heap allocation per block.
    std::vector<double> fp_block_;
};

}  // namespace audio::usecase::stages
