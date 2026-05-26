#include "OutputStage.h"

#include <algorithm>

namespace audio::usecase::stages {

OutputStage::OutputStage(std::shared_ptr<port::IAudioOutput> output,
                         std::shared_ptr<port::IRecorder> recorder,
                         std::shared_ptr<port::IVideoOutput> video_out, int sample_rate)
    : output_(std::move(output)),
      recorder_(std::move(recorder)),
      video_out_(std::move(video_out)),
      sample_rate_(sample_rate) {}

void OutputStage::process(BlockContext& ctx) {
    if (!output_ || ctx.channel_outputs.empty()) {
        return;
    }

    const auto num_channels = ctx.channel_outputs.size();
    const auto frames = ctx.channel_outputs[0].size();
    const auto total = frames * num_channels;

    // Resize only if needed — typically a no-op after the first block.
    interleaved_.resize(total);

    for (std::size_t fr = 0; fr < frames; ++fr) {
        for (std::size_t ch = 0; ch < num_channels; ++ch) {
            const auto& col = ctx.channel_outputs[ch];
            interleaved_[(fr * num_channels) + ch] = (fr < col.size()) ? col[fr] : 0.0;
        }
    }

    output_->write(interleaved_);

    {
        std::scoped_lock lock(recorder_mutex_);
        if (recorder_ && recorder_->isOpen()) {
            recorder_->write(interleaved_);
        }
    }

    if (video_out_ != nullptr && ctx.match != nullptr) {
        const double block_dur = static_cast<double>(frames) / static_cast<double>(sample_rate_);
        video_out_->onBlock(ctx.match->video, block_dur, ctx.audio_start_sec);
    }

    total_samples_written_.fetch_add(total, std::memory_order_relaxed);
}

void OutputStage::setRecorder(std::shared_ptr<port::IRecorder> recorder) {
    std::scoped_lock lock(recorder_mutex_);
    recorder_ = std::move(recorder);
}

}  // namespace audio::usecase::stages
