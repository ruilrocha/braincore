#include "SoundProcessor.h"

#include "../domain/WindowFunction.h"
#include "EffectHelpers.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace audio::usecase {

SoundProcessor::SoundProcessor(std::shared_ptr<port::ISearchStrategy> search,
                               const SearchParams& params, BlockConfig target_config,
                               std::shared_ptr<port::IBlockEffect> spectral_morph,
                               std::shared_ptr<port::IVideoOutput> video_output)
    : search_(std::move(search)),
      params_(params),
      target_config_(target_config),
      spectral_morph_(std::move(spectral_morph)),
      video_output_(std::move(video_output)) {}

// ── Main processing pipeline ───────────────────────────────────────────

Sound SoundProcessor::process(const std::shared_ptr<const Brain>& brain, const Sound& target,
                              const ProgressFn& on_progress) const {
    const auto& target_channels = target.getChannels();
    if (target_channels.empty() || !brain) {
        return {std::vector<Channel>{}, target.getSampleRate()};
    }

    const auto bs = static_cast<std::size_t>(target_config_.block_size);
    const auto ovl = static_cast<std::size_t>(
        std::max(0, std::min(target_config_.overlap, target_config_.block_size - 1)));
    const auto step = bs - ovl;

    const auto& ch0 = target.getChannel(0);
    const int sample_rate = target.getSampleRate();
    const auto num_channels = static_cast<std::size_t>(target.getNumChannels());
    const double alpha = params_.alpha;

    // Check which effects are active.
    const bool do_granular = params_.grain_size < 1.0 || params_.grain_scatter > 0.0;
    const bool do_morph = spectral_morph_ && params_.spectral_morph > 0.0;
    const bool do_stutter = params_.stutter_chance > 0.0;
    const bool do_envelope = params_.envelope_shape > 0 && params_.envelope_amount > 0.0;

    // 1. Determine how many blocks fit in the target.
    std::size_t num_blocks = 0;
    if (ch0.size() >= bs) {
        num_blocks = ((ch0.size() - bs) / step) + 1;
    }
    if (num_blocks == 0) {
        return {std::vector<Channel>{}, sample_rate};
    }

    // 2. Allocate output buffers.
    const std::size_t output_samples = ((num_blocks - 1) * step) + bs;
    std::vector output_channels(num_channels, Channel(output_samples, 0.0));
    std::vector weight_acc(output_samples, 0.0);

    // 3. Single-pass: match + synthesise + overlap-add per block.
    PlayHead ph(brain, search_);

    // Track previous matched samples per channel for spectral morph.
    std::vector<std::vector<double>> prev_ch_samples(num_channels);

    for (std::size_t block_idx = 0; block_idx < num_blocks; ++block_idx) {
        const auto offset = static_cast<std::ptrdiff_t>(block_idx * step);

        // ── Analysis ──────────────────────────────────────────────────
        std::vector block_samples(ch0.begin() + offset,
                                  ch0.begin() + offset + static_cast<std::ptrdiff_t>(bs));
        WindowFunction::apply(block_samples, target_config_.window);
        const auto fp = brain->analyser().compute(block_samples, sample_rate);

        // ── Search ────────────────────────────────────────────────────
        const std::size_t match_idx = ph.advance(fp, params_);
        const Block& match = brain->blocks()[match_idx];

        // ── Video notification ─────────────────────────────────────────
        if (video_output_) {
            const std::size_t samples = (block_idx < num_blocks - 1) ? step : bs;
            const double block_dur =
                static_cast<double>(samples) / static_cast<double>(sample_rate);
            video_output_->onBlock(match.video, block_dur);
        }

        // ── Synthesis + overlap-add per channel ────────────────────────
        const std::size_t out_offset = block_idx * step;
        const std::size_t tgt_offset = block_idx * step;

        for (std::size_t ch = 0; ch < num_channels; ++ch) {
            const auto& tgt_ch = target.getChannel(static_cast<int>(ch));

            const std::vector<double>* src_ptr = &match.samples;
            if (ch < match.channel_samples.size() && !match.channel_samples[ch].empty()) {
                src_ptr = &match.channel_samples[ch];
            }
            std::vector<double> src = *src_ptr;

            // ── Granular scatter ───────────────────────────────────────
            if (do_granular) {
                src = effects::granularScatter(
                    src, bs, params_.grain_size, params_.grain_scatter, params_.grain_density,
                    params_.grain_size_variation, params_.grain_amp_variation,
                    params_.grain_pitch_jitter, params_.grain_hop_randomness);
            }

            // ── Spectral morph with previous block ─────────────────────
            if (do_morph && !prev_ch_samples[ch].empty()) {
                src = spectral_morph_->apply(prev_ch_samples[ch], src, params_.spectral_morph);
            }

            // ── Stutter ────────────────────────────────────────────────
            if (do_stutter) {
                effects::applyStutter(src, params_.stutter_chance, params_.stutter_count);
            }

            // ── Envelope shaping ───────────────────────────────────────
            if (do_envelope) {
                effects::applyEnvelope(src, params_.envelope_shape, params_.envelope_amount);
            }

            prev_ch_samples[ch] = src;

            // ── Overlap-add with triangular cross-fade ─────────────────
            for (std::size_t i = 0; i < bs; ++i) {
                double env = 1.0;
                if (ovl > 0) {
                    if (i < ovl) {
                        env = static_cast<double>(i) / static_cast<double>(ovl);
                    } else if (i >= bs - ovl) {
                        env = static_cast<double>(bs - 1 - i) / static_cast<double>(ovl);
                    }
                }

                const double src_val = (i < src.size()) ? src[i] : 0.0;
                const double tgt_val =
                    (tgt_offset + i < tgt_ch.size()) ? tgt_ch[tgt_offset + i] : 0.0;
                const double blended = (alpha * src_val) + ((1.0 - alpha) * tgt_val);

                output_channels[ch][out_offset + i] += blended * env;

                if (ch == 0) {
                    weight_acc[out_offset + i] += env;
                }
            }
        }

        if (on_progress != nullptr) {
            on_progress(block_idx + 1, num_blocks);
        }
    }

    // 4. Normalise by accumulated weights.
    for (std::size_t ch = 0; ch < num_channels; ++ch) {
        for (std::size_t i = 0; i < output_samples; ++i) {
            if (weight_acc[i] > 0.0) {
                output_channels[ch][i] /= weight_acc[i];
            }
        }
    }

    return {std::move(output_channels), sample_rate};
}

}  // namespace audio::usecase
