#include "StreamProcessor.h"

#include "../domain/Random.h"
#include "../domain/WindowFunction.h"
#include "EffectHelpers.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace audio::usecase {

StreamProcessor::StreamProcessor(SearchParams params, BlockConfig target_config,
                                 std::shared_ptr<port::IAudioOutput> output,
                                 std::shared_ptr<port::IBlockEffect> spectral_morph,
                                 std::shared_ptr<port::IParamController> param_controller,
                                 std::shared_ptr<port::IRecorder> recorder,
                                 std::shared_ptr<port::IVideoOutput> video_output)
    : params_(params),
      target_config_(target_config),
      output_(std::move(output)),
      spectral_morph_(std::move(spectral_morph)),
      param_controller_(std::move(param_controller)),
      recorder_(std::move(recorder)),
      video_output_(std::move(video_output)) {}

// ── activeParams ───────────────────────────────────────────────────────

SearchParams StreamProcessor::activeParams() const {
    if (param_controller_) {
        return param_controller_->getParams();
    }
    return params_;
}

// ── applyEffects ───────────────────────────────────────────────────────

std::vector<double> StreamProcessor::applyEffects(const std::vector<double>& src,
                                                  const std::size_t block_size,
                                                  const SearchParams& params) {
    std::vector<double> out = src;

    // Granular scatter.
    if (params.grain_size < 1.0 || params.grain_scatter > 0.0) {
        out = effects::granularScatter(out, block_size, params.grain_size, params.grain_scatter,
                                       params.grain_density, params.grain_size_variation,
                                       params.grain_amp_variation, params.grain_pitch_jitter,
                                       params.grain_hop_randomness);
    }

    // Spectral morph (via injected adapter).
    if (spectral_morph_ && params.spectral_morph > 0.0 && !prev_block_.empty()) {
        out = spectral_morph_->apply(prev_block_, out, params.spectral_morph);
    }
    prev_block_ = out;

    // Stutter.
    effects::applyStutter(out, params.stutter_chance, params.stutter_count);

    // Envelope shaping.
    effects::applyEnvelope(out, params.envelope_shape, params.envelope_amount);

    return out;
}

// ── outputBlock ────────────────────────────────────────────────────────

void StreamProcessor::outputBlock(const std::vector<std::vector<double>>& channel_blocks) const {
    if (!output_ || channel_blocks.empty()) {
        return;
    }

    const auto num_channels = channel_blocks.size();
    const auto frames = channel_blocks[0].size();

    // Interleave channels.
    std::vector<double> interleaved(frames * num_channels);
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t ch = 0; ch < num_channels; ++ch) {
            interleaved[(f * num_channels) + ch] =
                (f < channel_blocks[ch].size()) ? channel_blocks[ch][f] : 0.0;
        }
    }

    output_->write(interleaved);

    // Tee to recorder if active (thread-safe access).
    {
        std::scoped_lock lock(recorder_mutex_);
        if (recorder_ && recorder_->isOpen()) {
            recorder_->write(interleaved);
        }
    }
}

// ── setRecorder ────────────────────────────────────────────────────────

void StreamProcessor::setRecorder(std::shared_ptr<port::IRecorder> recorder) {
    std::scoped_lock lock(recorder_mutex_);
    recorder_ = std::move(recorder);
}

// ── stream (target-driven, loops forever) ──────────────────────────────

bool StreamProcessor::stream(Brain& brain, const Sound& target) {
    if (!output_) {
        return false;
    }

    const int sample_rate = target.getSampleRate();
    const auto num_channels = static_cast<std::size_t>(target.getNumChannels());
    const auto bs = static_cast<std::size_t>(target_config_.block_size);
    const auto& ch0 = target.getChannel(0);
    const auto& analyser = brain.analyser();

    if (!output_->open(sample_rate, static_cast<int>(num_channels), static_cast<int>(bs))) {
        return false;
    }

    running_ = true;
    prev_block_.clear();

    const auto ovl = static_cast<std::size_t>(
        std::max(0, std::min(target_config_.overlap, target_config_.block_size - 1)));
    const auto step = bs - ovl;

    // Loop over the target continuously until stop() is called.
    while (running_) {
        for (std::size_t pos = 0; pos + bs <= ch0.size() && running_; pos += step) {
            // Snapshot live params for this block.
            const auto params = activeParams();

            // Fingerprint channel 0.
            std::vector<double> fp_block(ch0.begin() + static_cast<std::ptrdiff_t>(pos),
                                         ch0.begin() + static_cast<std::ptrdiff_t>(pos + bs));
            WindowFunction::apply(fp_block, target_config_.window);
            auto fp = analyser.compute(fp_block, sample_rate);

            const auto& match = brain.findBestMatch(fp, params);

            // Build per-channel output.
            std::vector<std::vector<double>> out_channels(num_channels);
            for (std::size_t ch = 0; ch < num_channels; ++ch) {
                const auto& tgt_ch = target.getChannel(static_cast<int>(ch));

                const std::vector<double>* src_ptr = &match.samples;
                if (ch < match.channel_samples.size() && !match.channel_samples[ch].empty()) {
                    src_ptr = &match.channel_samples[ch];
                }

                auto src = applyEffects(*src_ptr, bs, params);

                // Alpha-blend with target.
                const double alpha = params.alpha;
                out_channels[ch].resize(bs);
                for (std::size_t i = 0; i < bs; ++i) {
                    const double sv = (i < src.size()) ? src[i] : 0.0;
                    const double tv = (pos + i < tgt_ch.size()) ? tgt_ch[pos + i] : 0.0;
                    out_channels[ch][i] = (alpha * sv) + ((1.0 - alpha) * tv);
                }
            }

            outputBlock(out_channels);

            // Notify video output with the matched block's video segment.
            if (video_output_) {
                const double block_dur = static_cast<double>(bs) / static_cast<double>(sample_rate);
                video_output_->onBlock(match.video, block_dur);
            }
        }
        // Target exhausted — loop back to the beginning.
    }

    // Wait for the ring buffer to drain.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    output_->close();
    if (video_output_) {
        video_output_->close();
    }
    {
        std::scoped_lock lock(recorder_mutex_);
        if (recorder_) {
            recorder_->close();
        }
    }
    running_ = false;
    return true;
}

// ── streamInfinite (generative) ────────────────────────────────────────

void StreamProcessor::streamInfinite(Brain& brain, const int sample_rate, const int channels) {
    if (!output_ || brain.empty()) {
        return;
    }

    const auto bs = static_cast<std::size_t>(target_config_.block_size);
    const auto num_ch = static_cast<std::size_t>(channels);

    if (!output_->open(sample_rate, channels, static_cast<int>(bs))) {
        return;
    }

    running_ = true;
    prev_block_.clear();

    // Seed with a random block's fingerprint.
    brain.jiggle();
    std::vector<double> search_fp = brain.blocks()[brain.currentBlockIndex()].mfcc;

    // Compute the typical scale of fingerprint values so drift is meaningful.
    double fp_scale = 0.0;
    for (const double v : search_fp) {
        fp_scale += v * v;
    }
    fp_scale =
        std::sqrt(fp_scale / static_cast<double>(std::max(search_fp.size(), std::size_t{1})));
    if (fp_scale < 1e-6) {
        fp_scale = 1.0;
    }

    std::size_t step_count = 0;
    std::size_t prev_match_idx = brain.currentBlockIndex();
    std::size_t stuck_count = 0;

    while (running_) {
        // Snapshot live params for this block.
        const auto params = activeParams();

        // Build infinite-mode overrides.
        SearchParams inf_params = params;
        inf_params.usage_weight = std::max(params.usage_weight, 0.1);
        const double inf_usage_falloff = std::min(params.usage_falloff, 0.995);

        const auto& match = brain.findBestMatch(search_fp, inf_params);
        const std::size_t match_idx = brain.currentBlockIndex();

        // Detect being stuck on the same block.
        if (match_idx == prev_match_idx) {
            ++stuck_count;
        } else {
            stuck_count = 0;
        }
        prev_match_idx = match_idx;

        // If stuck for too many steps, jiggle to break out.
        if (stuck_count > 3) {
            brain.jiggle();
            search_fp = brain.blocks()[brain.currentBlockIndex()].mfcc;
            stuck_count = 0;
        }

        // Evolve search fingerprint: blend toward match + additive drift.
        const auto& match_fp = match.mfcc;
        search_fp.resize(match_fp.size());

        const double drift_amount =
            0.15 + (0.05 * std::sin(static_cast<double>(step_count) * 0.01));

        for (std::size_t i = 0; i < match_fp.size(); ++i) {
            const double noise = (rng::randomDouble() - 0.5) * 2.0;
            search_fp[i] = match_fp[i] + (noise * drift_amount * fp_scale);
        }

        // Deplete usage so blocks gradually become available again.
        brain.depleteUsage(inf_usage_falloff);

        // Output audio.
        std::vector<std::vector<double>> out_channels(num_ch);
        for (std::size_t ch = 0; ch < num_ch; ++ch) {
            const std::vector<double>* src_ptr = &match.samples;
            if (ch < match.channel_samples.size() && !match.channel_samples[ch].empty()) {
                src_ptr = &match.channel_samples[ch];
            }
            out_channels[ch] = applyEffects(*src_ptr, bs, inf_params);
        }

        outputBlock(out_channels);

        // Notify video output with the matched block's video segment.
        if (video_output_) {
            const double block_dur = static_cast<double>(bs) / static_cast<double>(sample_rate);
            video_output_->onBlock(match.video, block_dur);
        }

        ++step_count;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    output_->close();
    if (video_output_) {
        video_output_->close();
    }
    {
        std::scoped_lock lock(recorder_mutex_);
        if (recorder_) {
            recorder_->close();
        }
    }
}

// ── stop ───────────────────────────────────────────────────────────────

void StreamProcessor::stop() {
    running_ = false;
}

}  // namespace audio::usecase
