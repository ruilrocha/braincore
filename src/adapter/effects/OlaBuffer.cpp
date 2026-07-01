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

void OlaBuffer::accumulate(const std::vector<std::vector<float>>& channel_samples) {
    if (!active() || channel_samples.empty()) {
        return;
    }

    const std::size_t nch = channel_samples.size();

    // One-time allocation — channel count not known at construction.
    if (buffers_.size() < nch) {
        buffers_.assign(nch, std::vector(buf_size_, 0.0));
        read_buf_.assign(nch, std::vector(step_size_, 0.0));
    }

    const std::size_t len = std::min(channel_samples[0].size(), block_size_);

    // Split the write into at most two contiguous sub-ranges to avoid per-sample
    // modulo. write_pos_ always advances by step_size_ ≤ block_size_ ≤ buf_size_,
    // so the range [write_pos_, write_pos_ + len) wraps at most once.
    const std::size_t wrap = buf_size_ - write_pos_;  // samples until buffer end

    for (std::size_t chi = 0; chi < nch; ++chi) {
        const auto& src = channel_samples[chi];
        const std::size_t ch_len = std::min(src.size(), len);

        if (ch_len <= wrap) {
            // No wrap — single contiguous range.
            for (std::size_t i = 0; i < ch_len; ++i) {
                buffers_[chi][write_pos_ + i] += static_cast<double>(src[i]) * window_coeffs_[i];
            }
        } else {
            // Wraps once — two contiguous ranges.
            for (std::size_t i = 0; i < wrap; ++i) {
                buffers_[chi][write_pos_ + i] += static_cast<double>(src[i]) * window_coeffs_[i];
            }
            for (std::size_t i = wrap; i < ch_len; ++i) {
                buffers_[chi][i - wrap] += static_cast<double>(src[i]) * window_coeffs_[i];
            }
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

    // Split into at most two contiguous sub-ranges to avoid per-sample modulo.
    const std::size_t wrap = buf_size_ - read_pos_;  // samples until buffer end

    for (std::size_t chi = 0; chi < nch; ++chi) {
        if (out[chi].size() < step_size_) {
            out[chi].resize(step_size_);
        }
        auto& buf = buffers_[chi];
        auto& dst = out[chi];

        if (step_size_ <= wrap) {
            // No wrap.
            for (std::size_t i = 0; i < step_size_; ++i) {
                dst[i] = buf[read_pos_ + i];
                buf[read_pos_ + i] = 0.0;
            }
        } else {
            // Wraps once.
            for (std::size_t i = 0; i < wrap; ++i) {
                dst[i] = buf[read_pos_ + i];
                buf[read_pos_ + i] = 0.0;
            }
            for (std::size_t i = wrap; i < step_size_; ++i) {
                dst[i] = buf[i - wrap];
                buf[i - wrap] = 0.0;
            }
        }
    }

    read_pos_ = (read_pos_ + step_size_) % buf_size_;
    return step_size_;
}

void OlaBuffer::resetBuffer() {
    for (auto& ch : buffers_) {
        std::ranges::fill(ch, 0.0);
    }
    read_pos_ = 0;
    write_pos_ = 0;
}

}  // namespace audio::adapter::effects
