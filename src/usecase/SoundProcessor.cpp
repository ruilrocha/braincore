#include "SoundProcessor.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "../domain/WindowFunction.h"

namespace audio::usecase {

SoundProcessor::SoundProcessor(SearchParams params, BlockConfig target_config)
    : params_(std::move(params)),
      target_config_(std::move(target_config)) {}

Sound SoundProcessor::process(Brain& brain, const Sound& target) const {
    const auto& target_channels = target.getChannels();
    if (target_channels.empty()) {
        return {std::vector<Channel>{}, target.getSampleRate()};
    }

    const auto bs = static_cast<std::size_t>(target_config_.block_size);
    const auto ovl = static_cast<std::size_t>(
        std::max(0, std::min(target_config_.overlap, target_config_.block_size - 1)));
    const auto step = bs - ovl;

    const auto& ch0 = target.getChannel(0);
    const int sample_rate = target.getSampleRate();
    const auto& analyser = brain.analyser();
    const int num_channels = target.getNumChannels();
    const double alpha = params_.alpha;

    // 1. Determine how many blocks fit in the target.
    std::size_t num_blocks = 0;
    if (ch0.size() >= bs) {
        num_blocks = (ch0.size() - bs) / step + 1;
    }
    if (num_blocks == 0) {
        return {std::vector<Channel>{}, sample_rate};
    }

    // 2. Match each target block (channel 0) against the brain.
    //    Apply the target window before fingerprinting.
    std::vector<const Block*> matches(num_blocks);

    for (std::size_t b = 0; b < num_blocks; ++b) {
        const auto offset = static_cast<std::ptrdiff_t>(b * step);
        std::vector<double> block_samples(
            ch0.begin() + offset,
            ch0.begin() + offset + static_cast<std::ptrdiff_t>(bs));

        // Window the target block the same way the brain windowed its sources.
        WindowFunction::apply(block_samples, target_config_.window);

        auto fp = analyser.compute(block_samples, sample_rate);
        matches[b] = &brain.findBestMatch(fp, params_);
    }

    // 3. Reconstruct every channel using overlap-add.
    //    When overlap > 0, consecutive blocks are cross-faded with a
    //    triangular window to eliminate block-boundary clicks.
    const std::size_t output_samples = (num_blocks - 1) * step + bs;
    std::vector<Channel> output_channels(num_channels, Channel(output_samples, 0.0));

    // Weight accumulator for normalising the overlap-add.
    std::vector<double> weight_acc(output_samples, 0.0);

    for (int ch = 0; ch < num_channels; ++ch) {
        const auto& tgt_ch = target.getChannel(ch);

        for (std::size_t b = 0; b < num_blocks; ++b) {
            const std::size_t out_offset = b * step;
            const std::size_t tgt_offset = b * step;

            // Pick source samples: prefer multi-channel data from the
            // matched block if available, otherwise fall back to mono.
            const auto& match = *matches[b];
            const std::vector<double>* src_ptr = &match.samples;
            if (ch < static_cast<int>(match.channel_samples.size()) &&
                !match.channel_samples[ch].empty()) {
                src_ptr = &match.channel_samples[ch];
            }
            const auto& src = *src_ptr;

            for (std::size_t i = 0; i < bs; ++i) {
                // Triangular cross-fade envelope for overlap-add.
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
                const double blended = alpha * src_val + (1.0 - alpha) * tgt_val;

                output_channels[ch][out_offset + i] += blended * env;

                // Only accumulate weights once (channel 0).
                if (ch == 0) {
                    weight_acc[out_offset + i] += env;
                }
            }
        }
    }

    // 4. Normalise by accumulated weights (prevents amplitude build-up
    //    in overlapping regions).
    for (int ch = 0; ch < num_channels; ++ch) {
        for (std::size_t i = 0; i < output_samples; ++i) {
            if (weight_acc[i] > 0.0) {
                output_channels[ch][i] /= weight_acc[i];
            }
        }
    }

    return {std::move(output_channels), sample_rate};
}

} // namespace audio::usecase

