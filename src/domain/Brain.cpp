#include "Brain.h"

#include "WindowFunction.h"

#include <algorithm>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

namespace audio {

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
    if (blocks_.empty()) {
        return;
    }

    const std::size_t num_blocks = blocks_.size();
    const std::size_t num_neighbors = std::min(num_synapses, num_blocks > 0 ? num_blocks - 1 : 0);

    auto analyser = analyser_;
    auto dist_fn = [analyser](const auto& fingerprint_a, const auto& fingerprint_b) {
        return analyser->distance(fingerprint_a, fingerprint_b);
    };

    std::vector<std::vector<double>> fingerprints;
    fingerprints.reserve(num_blocks);
    for (const auto& block : blocks_) {
        fingerprints.push_back(block.analysis.print.mel);
    }

    index_.emplace();
    index_->build(std::move(fingerprints), dist_fn, num_neighbors);
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

    // Precompute Hann window coefficients once for all blocks in this sound.
    // Hann is hardcoded for analysis: it gives the best frequency resolution for
    // MFCC fingerprinting (low sidelobes, minimal spectral leakage).
    // The user-selected window shape is used separately for OLA synthesis output.
    const auto window_coefficients =
        WindowFunction::makeCoefficients(block_size, WindowShape::Hann);

    std::size_t block_index = 0;
    for (std::size_t i = 0; i < ch0.size(); i += step, ++block_index) {
        Block block;
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
            // Guard against channels shorter than ch0 (src_ch.size() - i would underflow).
            const auto ch_available =
                (src_ch.size() > i) ? std::min(block_size, src_ch.size() - i) : std::size_t{0};
            block.channel_samples[ch].assign(
                src_ch.begin() + static_cast<std::ptrdiff_t>(i),
                src_ch.begin() + static_cast<std::ptrdiff_t>(i + ch_available));
            block.channel_samples[ch].resize(block_size, 0.0);
        }

        // channel_samples[0] is the raw mono reference (zero-padded if needed).
        const auto& raw_ch0 = block.channel_samples[0];

        // ── Apply precomputed window and analyse ───────────────────────
        std::vector windowed(raw_ch0.begin(), raw_ch0.begin() + static_cast<long>(available));
        windowed.resize(block_size, 0.0);
        WindowFunction::applyCoefficients(windowed, window_coefficients);

        block.analysis.print = analyser_->analyse(windowed, sample_rate);

        // ── Normalised fingerprints (amplitude-invariant) ──────────────
        std::vector norm(raw_ch0.begin(), raw_ch0.begin() + static_cast<long>(available));
        norm.resize(block_size, 0.0);
        WindowFunction::normalise(norm);
        WindowFunction::applyCoefficients(norm, window_coefficients);

        block.analysis.normalised_print = analyser_->analyse(norm, sample_rate);

        blocks_.push_back(std::move(block));
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

std::vector<std::size_t> Brain::kNearest(const std::vector<double>& fingerprint,
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
