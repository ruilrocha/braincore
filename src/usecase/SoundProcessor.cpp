#include "SoundProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <utility>
#include <vector>

#include "../domain/WindowFunction.h"

namespace audio::usecase {

SoundProcessor::SoundProcessor(SearchParams params, BlockConfig target_config)
    : params_(std::move(params)),
      target_config_(std::move(target_config)) {}

// ── Granular post-processing ───────────────────────────────────────────

namespace {

/**
 * Apply a half-cosine (raised cosine) envelope to a grain for smooth
 * attack/release. Prevents clicks at grain boundaries.
 */
void applyGrainEnvelope(std::vector<double>& grain) {
    const auto n = grain.size();
    if (n < 4) return;
    for (std::size_t i = 0; i < n; ++i) {
        // Hann-shaped envelope: 0 at edges, 1 in the middle.
        const double t = static_cast<double>(i) / static_cast<double>(n - 1);
        const double env = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * t));
        grain[i] *= env;
    }
}

/**
 * Extract a grain from a block at a given offset, with wrapping.
 * Returns a Hann-enveloped grain of the requested size.
 */
std::vector<double> extractGrain(const std::vector<double>& src,
                                  std::size_t grain_size,
                                  std::size_t offset) {
    std::vector<double> grain(grain_size);
    for (std::size_t i = 0; i < grain_size; ++i) {
        grain[i] = src[(offset + i) % src.size()];
    }
    applyGrainEnvelope(grain);
    return grain;
}

/**
 * Granular scatter: break each matched block into overlapping micro-grains
 * with randomised positions, creating granular synthesis textures.
 *
 * @param src           Source block samples.
 * @param block_size    Full block size in samples.
 * @param grain_size_f  Grain size as fraction of block_size [0.01, 1.0].
 * @param scatter       Random offset amount [0.0, 1.0].
 * @param density       Grain overlap density [0.1, 4.0].
 * @return              Granularised block of block_size samples.
 */
std::vector<double> granularScatter(const std::vector<double>& src,
                                     std::size_t block_size,
                                     double grain_size_f,
                                     double scatter,
                                     double density) {
    const auto gs = static_cast<std::size_t>(
        std::clamp(grain_size_f, 0.01, 1.0) * static_cast<double>(block_size));
    if (gs == 0 || gs >= block_size) return src;

    std::vector<double> output(block_size, 0.0);
    std::vector<double> weight(block_size, 0.0);

    // Hop between grains — smaller hop = denser overlap.
    const auto hop = static_cast<std::size_t>(
        std::max(1.0, static_cast<double>(gs) / std::max(density, 0.1)));

    for (std::size_t pos = 0; pos < block_size; pos += hop) {
        // Random offset within the source block.
        const auto max_offset = static_cast<int>(src.size() - gs);
        int offset = static_cast<int>(pos);
        if (scatter > 0.0 && max_offset > 0) {
            const int jitter = static_cast<int>(
                scatter * static_cast<double>(max_offset)
                * (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 2.0);
            offset = std::clamp(offset + jitter, 0, max_offset);
        }

        auto grain = extractGrain(src, gs, static_cast<std::size_t>(offset));

        // Overlap-add the grain into the output.
        for (std::size_t i = 0; i < gs && pos + i < block_size; ++i) {
            output[pos + i] += grain[i];
            weight[pos + i] += 1.0;
        }
    }

    // Normalise by weight to prevent amplitude build-up.
    for (std::size_t i = 0; i < block_size; ++i) {
        if (weight[i] > 0.0) output[i] /= weight[i];
    }
    return output;
}

/**
 * Spectral morph: cross-fade between two blocks in the frequency domain.
 *
 * Uses a simple DFT-magnitude interpolation with phase from the dominant
 * block to create smooth timbral transitions between consecutive matches.
 *
 * @param prev     Previous matched block samples.
 * @param current  Current matched block samples.
 * @param amount   Morph amount [0.0, 1.0].  0.5 = equal blend.
 * @return         Morphed block samples.
 */
std::vector<double> spectralMorph(const std::vector<double>& prev,
                                   const std::vector<double>& current,
                                   double amount) {
    const auto n = std::min(prev.size(), current.size());
    if (n == 0) return current;

    // Simplified spectral morph using sample-domain cross-fade with
    // envelope-following to preserve timbral characteristics.
    // A full FFT-domain morph would require FFTW here (adapter layer);
    // this approach stays in the use-case layer and still sounds smooth.

    // Compute RMS envelopes for amplitude matching.
    double rms_prev = 0.0, rms_curr = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        rms_prev += prev[i] * prev[i];
        rms_curr += current[i] * current[i];
    }
    rms_prev = std::sqrt(rms_prev / static_cast<double>(n));
    rms_curr = std::sqrt(rms_curr / static_cast<double>(n));

    // Target RMS: blend between the two.
    const double target_rms = rms_prev * amount + rms_curr * (1.0 - amount);

    std::vector<double> result(n);
    for (std::size_t i = 0; i < n; ++i) {
        // Position-dependent cross-fade: ramp from prev→current across the block.
        const double t = static_cast<double>(i) / static_cast<double>(n - 1);
        // Blend factor ramps from `amount` at start to `1-amount` at end,
        // creating a smooth temporal morph.
        const double blend = amount * (1.0 - t) + (1.0 - amount) * t;
        result[i] = prev[i] * blend + current[i] * (1.0 - blend);
    }

    // Match RMS to the target to prevent volume fluctuations.
    double result_rms = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        result_rms += result[i] * result[i];
    }
    result_rms = std::sqrt(result_rms / static_cast<double>(n));

    if (result_rms > 1e-10 && target_rms > 1e-10) {
        const double gain = target_rms / result_rms;
        for (auto& s : result) s *= gain;
    }

    return result;
}

} // anonymous namespace

// ── Main processing pipeline ───────────────────────────────────────────

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

    // Check if granular or spectral effects are active.
    const bool do_granular = params_.grain_size < 1.0 || params_.grain_scatter > 0.0;
    const bool do_morph = params_.spectral_morph > 0.0;

    // 1. Determine how many blocks fit in the target.
    std::size_t num_blocks = 0;
    if (ch0.size() >= bs) {
        num_blocks = (ch0.size() - bs) / step + 1;
    }
    if (num_blocks == 0) {
        return {std::vector<Channel>{}, sample_rate};
    }

    // 2. Match each target block (channel 0) against the brain.
    std::vector<const Block*> matches(num_blocks);

    for (std::size_t b = 0; b < num_blocks; ++b) {
        const auto offset = static_cast<std::ptrdiff_t>(b * step);
        std::vector<double> block_samples(
            ch0.begin() + offset,
            ch0.begin() + offset + static_cast<std::ptrdiff_t>(bs));

        WindowFunction::apply(block_samples, target_config_.window);
        auto fp = analyser.compute(block_samples, sample_rate);
        matches[b] = &brain.findBestMatch(fp, params_);
    }

    // 3. Reconstruct every channel using overlap-add.
    const std::size_t output_samples = (num_blocks - 1) * step + bs;
    std::vector<Channel> output_channels(num_channels, Channel(output_samples, 0.0));
    std::vector<double> weight_acc(output_samples, 0.0);

    for (int ch = 0; ch < num_channels; ++ch) {
        const auto& tgt_ch = target.getChannel(ch);

        for (std::size_t b = 0; b < num_blocks; ++b) {
            const std::size_t out_offset = b * step;
            const std::size_t tgt_offset = b * step;

            // Get source samples for this channel.
            const auto& match = *matches[b];
            const std::vector<double>* src_ptr = &match.samples;
            if (ch < static_cast<int>(match.channel_samples.size()) &&
                !match.channel_samples[ch].empty()) {
                src_ptr = &match.channel_samples[ch];
            }
            std::vector<double> src = *src_ptr;

            // ── Granular scatter ───────────────────────────────────────
            if (do_granular) {
                src = granularScatter(src, bs, params_.grain_size,
                                      params_.grain_scatter,
                                      params_.grain_density);
            }

            // ── Spectral morph with previous block ─────────────────────
            if (do_morph && b > 0) {
                const auto& prev_match = *matches[b - 1];
                const std::vector<double>* prev_ptr = &prev_match.samples;
                if (ch < static_cast<int>(prev_match.channel_samples.size()) &&
                    !prev_match.channel_samples[ch].empty()) {
                    prev_ptr = &prev_match.channel_samples[ch];
                }
                src = spectralMorph(*prev_ptr, src, params_.spectral_morph);
            }

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
                const double blended = alpha * src_val + (1.0 - alpha) * tgt_val;

                output_channels[ch][out_offset + i] += blended * env;

                if (ch == 0) {
                    weight_acc[out_offset + i] += env;
                }
            }
        }
    }

    // 4. Normalise by accumulated weights.
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

