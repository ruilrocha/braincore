#include "AnalysisStage.h"

#include "../../domain/WindowFunction.h"

#include <algorithm>

namespace audio::usecase::stages {

AnalysisStage::AnalysisStage(const Sound& target, const BlockConfig& config,
                             const port::IAnalyser& analyser, int sample_rate)
    : target_(target),
      config_(config),
      analyser_(analyser),
      sample_rate_(sample_rate),
      step_(static_cast<std::size_t>(
          config.block_size - std::max(0, std::min(config.overlap, config.block_size - 1)))) {
    fp_block_.resize(static_cast<std::size_t>(config.block_size));
}

void AnalysisStage::process(BlockContext& ctx) {
    const auto& ch0 = target_.getChannel(0);
    const auto bs = static_cast<std::size_t>(config_.block_size);
    const std::size_t sample_offset = ctx.block_index * step_;

    // Copy target slice into pre-allocated buffer.
    const std::size_t available = (sample_offset < ch0.size()) ? ch0.size() - sample_offset : 0;
    const std::size_t to_copy = std::min(bs, available);
    std::copy_n(ch0.begin() + static_cast<std::ptrdiff_t>(sample_offset), to_copy,
                fp_block_.begin());
    if (to_copy < bs) {
        std::fill(fp_block_.begin() + static_cast<std::ptrdiff_t>(to_copy), fp_block_.end(), 0.0);
    }

    WindowFunction::apply(fp_block_, config_.window);
    ctx.fingerprint = analyser_.compute(fp_block_, sample_rate_);
}

}  // namespace audio::usecase::stages
