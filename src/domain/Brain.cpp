#include "Brain.h"

#include "WindowFunction.h"

#include <algorithm>
#include <ranges>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>

namespace audio {

namespace {

// Thin parallel-for that splits [start, end) across hardware threads.
// Falls back to serial when the range is small or only one core is available.
// Uses std::thread directly — avoids TBB / GCD / std::execution dependencies.
template <typename F>
void parallel_for(std::size_t start, std::size_t end, F&& fn) {
    if (start >= end) {
        return;
    }
    const auto span = end - start;
    const auto nthreads =
        std::min(static_cast<std::size_t>(std::max(1u, std::thread::hardware_concurrency())), span);
    if (nthreads <= 1) {
        for (std::size_t i = start; i < end; ++i) {
            fn(i);
        }
        return;
    }
    std::vector<std::thread> threads;
    threads.reserve(nthreads);
    const std::size_t chunk = (span + nthreads - 1) / nthreads;
    for (std::size_t t = 0; t < nthreads; ++t) {
        const std::size_t t_start = start + t * chunk;
        const std::size_t t_end = std::min(t_start + chunk, end);
        if (t_start >= end) {
            break;
        }
        threads.emplace_back([&fn, t_start, t_end]() {
            for (std::size_t i = t_start; i < t_end; ++i) {
                fn(i);
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
}

}  // namespace

Brain::Brain(std::shared_ptr<port::IAnalyser> analyser, const BlockConfig& config)
    : analyser_(std::move(analyser)), config_(config) {}

std::shared_ptr<Brain> Brain::rebuild(const std::vector<Block>& blocks,
                                      std::shared_ptr<port::IAnalyser> analyser,
                                      const BlockConfig& config) {
    auto brain = std::make_shared<Brain>(std::move(analyser), config);
    brain->blocks_ = blocks;
    brain->block_active_.assign(blocks.size(), true);  // all active; no source tracking
    return brain;
}

// ── Active-flag maintenance ────────────────────────────────────────────

void Brain::rebuildActiveFlags() {
    block_active_.assign(blocks_.size(), false);
    for (const auto& src : sources_) {
        if (!src.enabled) {
            continue;
        }
        const std::size_t end = std::min(src.end, blocks_.size());
        for (std::size_t i = src.start; i < end; ++i) {
            block_active_[i] = true;
        }
    }
}

// ── Index build ────────────────────────────────────────────────────────

void Brain::buildIndex(const std::size_t num_synapses) {
    if (blocks_.empty() || mel_dim_ == 0) {
        return;
    }

    const std::size_t num_blocks = blocks_.size();
    const std::size_t num_neighbors = std::min(num_synapses, num_blocks > 0 ? num_blocks - 1 : 0);

    // Pass a copy of the flat mel matrix to the index.
    // The matrix is already row-major (N × mel_dim), so no re-packing is needed.
    index_.emplace();
    index_->build(mel_matrix_, mel_dim_, num_neighbors);
}

// ── Ingestion ──────────────────────────────────────────────────────────

void Brain::addSound(const Sound& sound, const std::string& name,
                     const std::optional<VideoMetadata>& video) {
    if (sound.getNumChannels() == 0) {
        return;
    }

    const Channel& ch0 = sound.getChannel(0);
    const int sample_rate = sound.getSampleRate();
    const auto block_size = static_cast<std::size_t>(config_.block_size);
    const auto step = static_cast<std::size_t>(
        std::max(1, static_cast<int>(config_.block_size * (1.0 - config_.overlap))));

    SourceSound src;
    src.filename = name;
    src.start = blocks_.size();
    src.enabled = true;

    const int num_channels = sound.getNumChannels();

    const double block_duration_sec =
        static_cast<double>(config_.block_size) / static_cast<double>(sample_rate);
    const double step_sec = static_cast<double>(step) / static_cast<double>(sample_rate);

    // Reserve space for the new blocks up front to avoid repeated reallocation.
    // Each Block owns several heap vectors, so reallocs at large N are not free.
    const std::size_t num_new_blocks = (ch0.size() + step - 1) / step;
    blocks_.reserve(blocks_.size() + num_new_blocks);

    // ── Pass 1 (serial): segment samples, fill channel_samples ─────────
    // Sequential access pattern — fast and cache-friendly.
    // window_coefficients are computed once, shared (read-only) across threads.
    const auto window_coefficients =
        WindowFunction::makeCoefficients(block_size, WindowShape::Hann);

    const std::size_t new_start = blocks_.size();
    blocks_.resize(new_start + num_new_blocks);

    std::size_t block_index = 0;
    for (std::size_t i = 0; i < ch0.size(); i += step, ++block_index) {
        Block& block = blocks_[new_start + block_index];
        block.source_name = name;

        // ── Video metadata ─────────────────────────────────────────────
        if (video.has_value()) {
            block.video =
                VideoSegment{.source_path = video->path,
                             .offset_seconds = video->start_offset_seconds +
                                               (static_cast<double>(block_index) * step_sec),
                             .duration_seconds = block_duration_sec};
        }

        const auto available = std::min(block_size, ch0.size() - i);

        // ── Extract multi-channel samples for reconstruction ───────────
        block.channel_samples.resize(num_channels);
        for (int ch = 0; ch < num_channels; ++ch) {
            const auto& src_ch = sound.getChannel(ch);
            const auto ch_available =
                (src_ch.size() > i) ? std::min(block_size, src_ch.size() - i) : std::size_t{0};
            block.channel_samples[ch].assign(
                src_ch.begin() + static_cast<std::ptrdiff_t>(i),
                src_ch.begin() + static_cast<std::ptrdiff_t>(i + ch_available));
            block.channel_samples[ch].resize(block_size, 0.0F);  // zero-pad to block_size
        }
        (void)available;  // used implicitly: channel_samples[0] is now exactly block_size
    }

    // ── Pass 2 (parallel): fingerprint each block ───────────────────────
    // Blocks are accessed by index (no push_back races). Each thread gets its
    // own stack-local windowed/norm buffers. The analyser's filter-bank cache
    // is already protected by a shared_mutex (safe for concurrent reads).
    auto* analyser_ptr = analyser_.get();
    parallel_for(new_start, new_start + num_new_blocks, [&](std::size_t bi) {
        Block& block = blocks_[bi];
        const auto& raw_ch0 = block.channel_samples[0];
        // channel_samples[0] is already zero-padded to block_size — copy directly.
        std::vector<double> windowed(raw_ch0.begin(), raw_ch0.end());
        WindowFunction::applyCoefficients(windowed, window_coefficients);
        block.analysis.print = analyser_ptr->analyse(windowed, sample_rate);

        std::vector<double> norm(raw_ch0.begin(), raw_ch0.end());
        WindowFunction::normalise(norm);
        WindowFunction::applyCoefficients(norm, window_coefficients);
        block.analysis.normalised_print = analyser_ptr->analyse(norm, sample_rate);
    });

    // ── Pass 3 (serial): append mel rows to the flat matrix ─────────────
    // mel_matrix_ is N × mel_dim — enables cache-friendly O(N) mel scans.
    if (num_new_blocks > 0) {
        const std::size_t new_mel_dim = blocks_[new_start].analysis.print.mel.size();
        if (mel_dim_ == 0) {
            mel_dim_ = new_mel_dim;
        }
        mel_matrix_.reserve(mel_matrix_.size() + num_new_blocks * mel_dim_);
        for (std::size_t bi = new_start; bi < new_start + num_new_blocks; ++bi) {
            const auto& mel = blocks_[bi].analysis.print.mel;
            mel_matrix_.insert(mel_matrix_.end(), mel.begin(), mel.end());
        }
    }

    src.end = blocks_.size();
    src.num_blocks = src.end - src.start;
    sources_.push_back(std::move(src));

    // Extend active-flag vector; new blocks are active by default.
    block_active_.resize(blocks_.size(), true);
}

// ── Source management ──────────────────────────────────────────────────

void Brain::activateSound(const std::string& filename, const bool active) {
    bool changed = false;
    for (auto& sound : sources_) {
        if (sound.filename == filename && sound.enabled != active) {
            sound.enabled = active;
            changed = true;
        }
    }
    if (changed) {
        rebuildActiveFlags();
    }
}

bool Brain::isBlockActive(const std::size_t index) const {
    if (block_active_.empty()) {
        return true;  // no source tracking (e.g. after rebuild())
    }
    return index < block_active_.size() && block_active_[index];
}

// ── Index accessor delegators ──────────────────────────────────────────

std::vector<std::size_t> Brain::kNearest(const std::vector<float>& fingerprint,
                                         const std::size_t k_val) const {
    if (!index_.has_value()) {
        throw std::runtime_error("Brain::kNearest: index not built — call buildIndex() first.");
    }
    return index_->kNearest(fingerprint, k_val);
}

std::span<const std::size_t> Brain::neighbors(const std::size_t block_index) const {
    if (!index_.has_value()) {
        return {};
    }
    return index_->neighbors(block_index);
}

}  // namespace audio
