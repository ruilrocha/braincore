#include "OlaBuffer.h"

#include "../../domain/WindowFunction.h"

#include <algorithm>

namespace audio::adapter::effects {

OlaBuffer::OlaBuffer(const std::size_t block_size, const double overlap, const WindowShape window)
    : block_size_(block_size),
      overlap_(std::max(0.0, std::min(overlap, 0.9999))),
      step_size_(overlap_ > 0.0 ? std::max(std::size_t{1},
                                           static_cast<std::size_t>(
                                               static_cast<double>(block_size) * (1.0 - overlap_)))
                                : block_size),
      buf_size_(overlap_ > 0.0 ? block_size * 2 : 0),
      window_coeffs_(overlap_ > 0.0 ? WindowFunction::makeCoefficients(block_size, window)
                                    : std::vector<double>{}) {}

void OlaBuffer::accumulate(const std::vector<std::vector<double>>& channel_samples) {
    if (!active() || channel_samples.empty()) {
        return;
    }

    const std::size_t nch = channel_samples.size();

    // One-time allocation — channel count not known at construction.
    if (buffers_.size() < nch) {
        buffers_.assign(nch, std::vector<double>(buf_size_, 0.0));
        read_buf_.assign(nch, std::vector<double>(step_size_, 0.0));
    }

    const std::size_t len = std::min(channel_samples[0].size(), block_size_);

    for (std::size_t chi = 0; chi < nch; ++chi) {
        const auto& src = channel_samples[chi];
        const std::size_t ch_len = std::min(src.size(), len);
        for (std::size_t i = 0; i < ch_len; ++i) {
            const std::size_t pos = (write_pos_ + i) % buf_size_;
            buffers_[chi][pos] += src[i] * window_coeffs_[i];
        }
    }

    write_pos_ = (write_pos_ + step_size_) % buf_size_;
}

std::size_t OlaBuffer::read(std::vector<std::vector<double>>& out) {
    if (!active() || buffers_.empty()) {
        return 0;
    }

    const std::size_t nch = buffers_.size();
    if (out.size() < nch) {
        out.resize(nch);
    }

    for (std::size_t chi = 0; chi < nch; ++chi) {
        if (out[chi].size() < step_size_) {
            out[chi].resize(step_size_);
        }
        for (std::size_t i = 0; i < step_size_; ++i) {
            const std::size_t pos = (read_pos_ + i) % buf_size_;
            out[chi][i] = buffers_[chi][pos];
            buffers_[chi][pos] = 0.0;  // zero consumed region
        }
    }

    read_pos_ = (read_pos_ + step_size_) % buf_size_;
    return step_size_;
}

void OlaBuffer::reset() {
    for (auto& ch : buffers_) {
        std::ranges::fill(ch, 0.0);
    }
    read_pos_ = 0;
    write_pos_ = 0;
}

}  // namespace audio::adapter::effects
