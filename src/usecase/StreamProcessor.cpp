#include "StreamProcessor.h"

#include "../domain/PlayHead.h"
#include "../domain/Random.h"
#include "AudioPipeline.h"
#include "stages/AnalysisStage.h"
#include "stages/SearchStage.h"
#include "stages/SynthesisStage.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace audio::usecase {

// ── Constructor ────────────────────────────────────────────────────────

StreamProcessor::StreamProcessor(std::shared_ptr<const Brain> brain,
                                 std::shared_ptr<port::ISearchStrategy> search, SearchParams params,
                                 BlockConfig target_config,
                                 std::shared_ptr<port::IAudioOutput> output,
                                 std::shared_ptr<port::IBlockEffect> spectral_morph,
                                 std::shared_ptr<port::IParamController> param_controller,
                                 std::shared_ptr<port::IRecorder> recorder,
                                 std::shared_ptr<port::IVideoOutput> video_output)
    : brain_(std::move(brain)),
      search_(std::move(search)),
      params_(params),
      target_config_(target_config),
      output_(std::move(output)),
      spectral_morph_(std::move(spectral_morph)),
      param_controller_(std::move(param_controller)),
      video_output_(std::move(video_output)),
      recorder_(std::move(recorder)) {}

// ── Helpers ────────────────────────────────────────────────────────────

SearchParams StreamProcessor::activeParams() const {
    if (param_controller_) {
        return param_controller_->getParams();
    }
    return params_;
}

void StreamProcessor::cleanup() {
    output_stage_ = nullptr;
    output_->close();
    if (video_output_) {
        video_output_->close();
    }
    std::scoped_lock lock(recorder_mutex_);
    if (recorder_) {
        recorder_->close();
    }
}

void StreamProcessor::setRecorder(std::shared_ptr<port::IRecorder> recorder) {
    std::scoped_lock lock(recorder_mutex_);
    recorder_ = recorder;
    // Forward to the running stage if streaming.
    if (output_stage_ != nullptr) {
        output_stage_->setRecorder(std::move(recorder));
    }
}

void StreamProcessor::stop() {
    running_ = false;
}

// ── stream (target-driven, loops forever) ──────────────────────────────

bool StreamProcessor::stream(const Sound& target) {
    if (!output_) {
        return false;
    }

    const int sample_rate = target.getSampleRate();
    const auto num_channels = static_cast<std::size_t>(target.getNumChannels());
    const auto bs = static_cast<std::size_t>(target_config_.block_size);
    const auto ovl = static_cast<std::size_t>(
        std::max(0, std::min(target_config_.overlap, target_config_.block_size - 1)));
    const auto step = bs - ovl;
    const auto& ch0 = target.getChannel(0);

    if (!output_->open(sample_rate, static_cast<int>(num_channels), static_cast<int>(bs))) {
        return false;
    }

    // Build pipeline for this stream run.
    AudioPipeline pipeline;

    pipeline.addStage(std::make_unique<stages::AnalysisStage>(target, target_config_,
                                                              brain_->analyser(), sample_rate));

    auto* search_stage =
        pipeline.addStage(std::make_unique<stages::SearchStage>(PlayHead(brain_, search_)));

    pipeline.addStage(std::make_unique<stages::SynthesisStage>(target, target_config_, num_channels,
                                                               spectral_morph_));

    std::shared_ptr<port::IRecorder> rec;
    {
        std::scoped_lock lock(recorder_mutex_);
        rec = recorder_;
    }
    auto* out_stage = pipeline.addStage(
        std::make_unique<stages::OutputStage>(output_, rec, video_output_, sample_rate));
    output_stage_ = out_stage;

    running_ = true;
    const std::size_t num_blocks_in_target = ch0.size() >= bs ? ((ch0.size() - bs) / step) + 1 : 0;

    while (running_) {
        for (std::size_t block_idx = 0; block_idx < num_blocks_in_target && running_; ++block_idx) {
            if (brain_->empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            BlockContext ctx;
            ctx.block_index = block_idx;
            ctx.params = activeParams();
            ctx.audio_start_sec = static_cast<double>(output_stage_->totalSamplesWritten()) /
                                  static_cast<double>(static_cast<int>(num_channels) * sample_rate);

            pipeline.run(ctx);
        }
    }

    cleanup();
    return true;
}

// ── streamInfinite (generative) ────────────────────────────────────────

void StreamProcessor::streamInfinite(const int sample_rate, const int channels) {
    if (!output_ || brain_->empty()) {
        return;
    }

    const auto bs = static_cast<std::size_t>(target_config_.block_size);
    const auto num_ch = static_cast<std::size_t>(channels);

    if (!output_->open(sample_rate, channels, static_cast<int>(bs))) {
        return;
    }

    // Build a silent dummy target for SynthesisStage (alpha=1.0 in infinite mode
    // means the target samples are never mixed in — the buffer only needs to exist).
    // Must have num_ch channels so SynthesisStage::process() can index every channel.
    const Sound dummy_target{std::vector<Channel>(num_ch, Channel(bs * 2, 0.0)), sample_rate};

    // Build the pipeline (no AnalysisStage — fingerprint is injected each block).
    AudioPipeline pipeline;

    auto* search_stage_ptr =
        pipeline.addStage(std::make_unique<stages::SearchStage>(PlayHead(brain_, search_)));

    pipeline.addStage(std::make_unique<stages::SynthesisStage>(dummy_target, target_config_, num_ch,
                                                               spectral_morph_));

    std::shared_ptr<port::IRecorder> rec;
    {
        std::scoped_lock lock(recorder_mutex_);
        rec = recorder_;
    }
    auto* out_stage_ptr = pipeline.addStage(
        std::make_unique<stages::OutputStage>(output_, rec, video_output_, sample_rate));
    output_stage_ = out_stage_ptr;

    running_ = true;
    search_stage_ptr->playhead().reset();

    const std::size_t seed_idx = rng::randomIndex(brain_->size());
    std::vector<double> search_fp = brain_->blocks()[seed_idx].print.mfcc;

    double fp_scale = 0.0;
    for (const double fp_val : search_fp) {
        fp_scale += fp_val * fp_val;
    }
    fp_scale =
        std::sqrt(fp_scale / static_cast<double>(std::max(search_fp.size(), std::size_t{1})));
    if (fp_scale < 1e-6) {
        fp_scale = 1.0;
    }

    std::size_t step_count = 0;
    std::size_t prev_match_idx = seed_idx;
    std::size_t stuck_count = 0;

    while (running_) {
        const auto params = activeParams();

        SearchParams inf_params = params;
        inf_params.usage_weight = std::max(params.usage_weight, 0.1);
        inf_params.alpha = 1.0;  // full replacement — dummy target is never read
        const double inf_usage_falloff = std::min(params.usage_falloff, 0.995);

        BlockContext ctx;
        ctx.block_index = 0;
        ctx.params = inf_params;
        ctx.audio_start_sec = static_cast<double>(out_stage_ptr->totalSamplesWritten()) /
                              static_cast<double>(static_cast<int>(num_ch) * sample_rate);
        ctx.fingerprint = search_fp;

        pipeline.run(ctx);

        const std::size_t match_idx = ctx.match_idx;
        if (match_idx == prev_match_idx) {
            ++stuck_count;
        } else {
            stuck_count = 0;
        }
        prev_match_idx = match_idx;

        if (stuck_count > 3) {
            const std::size_t rand_idx = rng::randomIndex(brain_->size());
            search_fp = brain_->blocks()[rand_idx].print.mfcc;
            search_stage_ptr->playhead().reset();
            stuck_count = 0;
        }

        const auto& match_fp = ctx.match->print.mfcc;
        search_fp.resize(match_fp.size());
        const double drift = 0.15 + (0.05 * std::sin(static_cast<double>(step_count) * 0.01));
        for (std::size_t i = 0; i < match_fp.size(); ++i) {
            search_fp[i] = match_fp[i] + ((rng::randomDouble() - 0.5) * 2.0 * drift * fp_scale);
        }

        search_stage_ptr->playhead().depleteUsages(inf_usage_falloff);
        ++step_count;
    }

    cleanup();
}

}  // namespace audio::usecase
