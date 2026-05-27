#include "SynthesisStage.h"

#include "../EffectHelpers.h"

#include <algorithm>

namespace audio::usecase::stages {

SynthesisStage::SynthesisStage(const Sound& target, const BlockConfig& config,
                               std::size_t num_channels,
                               std::shared_ptr<port::IBlockEffect> spectral_morph)
    : target_(target),
      config_(config),
      num_channels_(num_channels),
      spectral_morph_(std::move(spectral_morph)),
      step_(static_cast<std::size_t>(config.block_size -
                                     std::max(0, std::min(config.overlap, config.block_size - 1)))),
      prev_ch_samples_(num_channels) {}

void SynthesisStage::process(BlockContext& ctx) {
    const auto& match = *ctx.match;
    const auto& params = ctx.params;
    const auto bs = static_cast<std::size_t>(config_.block_size);
    const std::size_t tgt_offset = ctx.block_index * step_;

    const bool do_granular = params.grain_size < 1.0 || params.grain_scatter > 0.0;
    const bool do_morph = spectral_morph_ && params.spectral_morph > 0.0;
    const bool do_stutter = params.stutter_chance > 0.0;
    const bool do_envelope = params.envelope_shape > 0 && params.envelope_amount > 0.0;

    ctx.channel_outputs.resize(num_channels_);

    for (std::size_t ch = 0; ch < num_channels_; ++ch) {
        const auto& tgt_ch = target_.getChannel(static_cast<int>(ch));

        // Select source samples for this channel.
        const std::vector<double>* src_ptr = &match.samples;
        if (ch < match.channel_samples.size() && !match.channel_samples[ch].empty()) {
            src_ptr = &match.channel_samples[ch];
        }
        std::vector<double> src = *src_ptr;

        if (do_granular) {
            src = effects::granularScatter(src, bs, params.grain_size, params.grain_scatter,
                                           params.grain_density, params.grain_size_variation,
                                           params.grain_amp_variation, params.grain_pitch_jitter,
                                           params.grain_hop_randomness);
        }

        if (do_morph && !prev_ch_samples_[ch].empty()) {
            src = spectral_morph_->apply(prev_ch_samples_[ch], src, params.spectral_morph);
        }
        prev_ch_samples_[ch] = src;

        if (do_stutter) {
            effects::applyStutter(src, params.stutter_chance, params.stutter_count);
        }

        if (do_envelope) {
            effects::applyEnvelope(src, params.envelope_shape, params.envelope_amount);
        }

        // Alpha-blend with target.
        ctx.channel_outputs[ch].resize(bs);
        for (std::size_t i = 0; i < bs; ++i) {
            const double sv = (i < src.size()) ? src[i] : 0.0;
            const double tv = (tgt_offset + i < tgt_ch.size()) ? tgt_ch[tgt_offset + i] : 0.0;
            ctx.channel_outputs[ch][i] = (params.alpha * sv) + ((1.0 - params.alpha) * tv);
        }
    }
}

void SynthesisStage::reset() {
    for (auto& ch_buf : prev_ch_samples_) {
        ch_buf.clear();
    }
}

}  // namespace audio::usecase::stages
