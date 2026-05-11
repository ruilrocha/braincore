#include "Brain.h"

#include "WindowFunction.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace audio {

Brain::Brain(std::shared_ptr<port::IAnalyser> analyser, const BlockConfig config)
    : analyser_(std::move(analyser)), config_(config) {}

std::shared_ptr<Brain> Brain::rebuild(const std::vector<Block>& blocks,
                                      std::shared_ptr<port::IAnalyser> analyser,
                                      const BlockConfig config) {
    auto brain = std::make_shared<Brain>(std::move(analyser), config);
    brain->blocks_ = blocks;  // copy — source Brain (and its audio thread) remains valid
    return brain;
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
    const auto step = static_cast<std::size_t>(config_.block_size - config_.overlap);

    // Track source metadata.
    SourceSound src;
    src.filename = name;
    src.start = blocks_.size();

    const int num_channels = sound.getNumChannels();

    // Block duration in seconds (used to stamp video offsets).
    const double block_duration_sec =
        static_cast<double>(config_.block_size) / static_cast<double>(sample_rate);
    const double step_sec = static_cast<double>(step) / static_cast<double>(sample_rate);

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

        // Determine how many samples are available from this position.
        const auto available = std::min(block_size, ch0.size() - i);

        // ── Extract mono samples (channel 0) for fingerprinting ────────
        std::vector raw_samples(ch0.begin() + static_cast<std::ptrdiff_t>(i),
                                ch0.begin() + static_cast<std::ptrdiff_t>(i + available));
        // Pad with silence if shorter than block size.
        raw_samples.resize(block_size, 0.0);

        // ── Extract multi-channel samples for reconstruction ───────────
        block.channel_samples.resize(num_channels);
        for (int ch = 0; ch < num_channels; ++ch) {
            const auto& src_ch = sound.getChannel(ch);
            const auto ch_available = std::min(block_size, src_ch.size() - i);
            block.channel_samples[ch].assign(
                src_ch.begin() + static_cast<std::ptrdiff_t>(i),
                src_ch.begin() + static_cast<std::ptrdiff_t>(i + ch_available));
            block.channel_samples[ch].resize(block_size, 0.0);
        }

        // ── Apply window to raw samples before analysis ────────────────
        std::vector<double> windowed = raw_samples;
        WindowFunction::apply(windowed, config_.window);

        // ── Compute raw fingerprints via the generic analyse() port ────
        block.print = analyser_->analyse(windowed, sample_rate);

        // ── Compute normalised fingerprints ────────────────────────────
        std::vector<double> norm_samples = raw_samples;
        WindowFunction::normalise(norm_samples);
        WindowFunction::apply(norm_samples, config_.window);

        block.normalised_print = analyser_->analyse(norm_samples, sample_rate);

        // Store mono samples (windowed version is only for analysis).
        block.samples = std::move(raw_samples);

        blocks_.push_back(std::move(block));
    }

    src.end = blocks_.size();
    src.num_blocks = src.end - src.start;
    sources_.push_back(std::move(src));
}

// ── Source management ──────────────────────────────────────────────────

void Brain::activateSound(const std::string& filename, const bool active) {
    for (auto& sound : sources_) {
        if (sound.filename == filename) {
            sound.enabled = active;
        }
    }
}

bool Brain::isBlockActive(const std::size_t index) const {
    // If no sources are tracked (e.g. after rebuild()), all blocks are considered active.
    if (sources_.empty()) {
        return true;
    }
    return std::ranges::any_of(sources_, [index](const auto& sound) {
        return index >= sound.start && index < sound.end && sound.enabled;
    });
}

}  // namespace audio
