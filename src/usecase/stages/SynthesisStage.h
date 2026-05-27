#pragma once

#include "../../domain/BlockConfig.h"
#include "../../domain/Sound.h"
#include "../../domain/port/IBlockEffect.h"
#include "../IBlockStage.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace audio::usecase::stages {

/**
 * Pipeline stage 3: Synthesis.
 *
 * For each output channel, fetches the matched block's source samples,
 * applies post-processing effects (granular scatter, spectral morph,
 * stutter, envelope), and alpha-blends with the target.
 *
 * Writes per-channel results to ctx.channel_outputs.
 *
 * Stateful: tracks the previous block's processed samples per channel for
 * spectral morph cross-fade.  Reset by calling reset().
 */
class SynthesisStage final : public IBlockStage {
public:
    /**
     * @param target        Target sound for alpha-blend (reference; must outlive this object).
     * @param config        Block config (block_size, overlap).
     * @param num_channels  Number of output channels.
     * @param spectral_morph Optional spectral morph effect adapter.
     */
    SynthesisStage(const Sound& target, const BlockConfig& config, std::size_t num_channels,
                   std::shared_ptr<port::IBlockEffect> spectral_morph = nullptr);

    void process(BlockContext& ctx) override;

    /// Clear per-channel previous-block state (call on stream restart).
    void reset();

private:
    const Sound& target_;
    BlockConfig config_;
    std::size_t num_channels_;
    std::shared_ptr<port::IBlockEffect> spectral_morph_;

    std::size_t step_;

    // Previous processed samples per channel — used for spectral morph.
    std::vector<std::vector<double>> prev_ch_samples_;
};

}  // namespace audio::usecase::stages
