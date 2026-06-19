#include "OutputPipeline.h"

#include <algorithm>

namespace audio {

// ── Construction ──────────────────────────────────────────────────────────────

OutputPipeline::OutputPipeline(const int block_size, const double overlap, const WindowShape window)
    : ola_buffer_(static_cast<std::size_t>(block_size), overlap, window) {}

// ── Effect management ─────────────────────────────────────────────────────────

void OutputPipeline::addEffect(const EffectType type, std::shared_ptr<port::IBlockEffect> effect) {
    effects_.add(type, std::move(effect));
}

void OutputPipeline::removeEffect(const EffectType type) noexcept {
    effects_.remove(type);
}

void OutputPipeline::setEffectAmount(const EffectType type, const double amount) noexcept {
    effects_.setAmount(type, amount);
}

// ── Push / read ───────────────────────────────────────────────────────────────

void OutputPipeline::push(const std::size_t matched_idx,
                          const std::vector<std::vector<float>>& channel_samples) {
    last_matched_idx_ = matched_idx;
    if (ola_buffer_.active()) {
        ola_buffer_.accumulate(channel_samples);
    }
}

std::size_t OutputPipeline::readInterleaved(const std::size_t index,
                                            const std::vector<Block>& blocks, double* out_buffer,
                                            const std::size_t max_frames) {
    if (out_buffer == nullptr || index >= blocks.size()) {
        return 0;
    }
    if (!last_matched_idx_.has_value()) {
        return 0;
    }

    const auto& channels = blocks[index].channel_samples;
    if (channels.empty()) {
        return 0;
    }
    const std::size_t nch = channels.size();

    // ── OLA path ─────────────────────────────────────────────────────────────
    if (ola_buffer_.active()) {
        const std::size_t step = ola_buffer_.stepSize();
        const std::size_t frames = std::min(step, max_frames);

        ola_buffer_.read(channel_scratch_);

        if (!effects_.empty()) {
            effects_.apply(channel_scratch_);
        }

        for (std::size_t i = 0; i < frames; ++i) {
            for (std::size_t ch = 0; ch < nch; ++ch) {
                out_buffer[(i * nch) + ch] = channel_scratch_[ch][i];
            }
        }
        return frames;
    }

    // ── Direct path ───────────────────────────────────────────────────────────
    const std::size_t frames = std::min(channels[0].size(), max_frames);

    if (!effects_.empty()) {
        if (channel_scratch_.size() < nch) {
            channel_scratch_.resize(nch);
        }
        for (std::size_t ch = 0; ch < nch; ++ch) {
            channel_scratch_[ch].assign(channels[ch].begin(), channels[ch].end());
        }

        effects_.apply(channel_scratch_);

        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t ch = 0; ch < nch; ++ch) {
                out_buffer[(frame * nch) + ch] = channel_scratch_[ch][frame];
            }
        }
        return frames;
    }

    // No effects: direct interleave.
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t ch = 0; ch < nch; ++ch) {
            out_buffer[(frame * nch) + ch] = channels[ch][frame];
        }
    }
    return frames;
}

std::size_t OutputPipeline::readMono(const std::size_t index, const std::vector<Block>& blocks,
                                     double* out_buffer, const std::size_t max_count) {
    if (out_buffer == nullptr || index >= blocks.size()) {
        return 0;
    }
    if (!last_matched_idx_.has_value()) {
        return 0;
    }

    const auto& ch_data = blocks[index].channel_samples;
    if (ch_data.empty()) {
        return 0;
    }
    const auto& src = ch_data[0];
    const std::size_t num_frames = std::min(src.size(), max_count);

    if (!effects_.empty()) {
        if (channel_scratch_.size() < 1) {
            channel_scratch_.resize(1);
        }
        channel_scratch_[0].assign(src.begin(), src.begin() + num_frames);
        channel_scratch_.resize(1);  // only ch0

        effects_.apply(channel_scratch_);

        const std::size_t copy_n = std::min(channel_scratch_[0].size(), num_frames);
        std::copy_n(channel_scratch_[0].begin(), static_cast<std::ptrdiff_t>(copy_n), out_buffer);
        return copy_n;
    }

    // No effects: direct copy.
    std::copy_n(src.begin(), static_cast<std::ptrdiff_t>(num_frames), out_buffer);
    return num_frames;
}

// ── State ─────────────────────────────────────────────────────────────────────

std::size_t OutputPipeline::stepSize() const noexcept {
    return ola_buffer_.active() ? ola_buffer_.stepSize() : 0;
}

bool OutputPipeline::olaActive() const noexcept {
    return ola_buffer_.active();
}

void OutputPipeline::reset() noexcept {
    ola_buffer_.resetBuffer();
    effects_.clearFeedback();
    last_matched_idx_.reset();
    channel_scratch_.clear();
}

}  // namespace audio
