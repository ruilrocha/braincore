#include "Brain.h"

#include "Random.h"
#include "WindowFunction.h"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace audio {

Brain::Brain(std::shared_ptr<port::IAnalyser> analyser,
             std::shared_ptr<port::ISearchStrategy> search, const BlockConfig config)
    : analyser_(std::move(analyser)), search_(std::move(search)), config_(config) {}

Brain Brain::rebuild(std::vector<Block> blocks, std::shared_ptr<port::IAnalyser> analyser,
                     std::shared_ptr<port::ISearchStrategy> strategy, BlockConfig config) {
    Brain brain(std::move(analyser), std::move(strategy), config);
    brain.blocks_ = std::move(blocks);
    // Synapses from the old brain are now stale (strategy changed); callers must
    // call buildSynapses() explicitly if the new strategy requires them.
    for (auto& block : brain.blocks_) {
        block.synapses.clear();
    }
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

std::vector<Block> Brain::takeBlocks() {
    return std::move(blocks_);
}

// ── Search ─────────────────────────────────────────────────────────────

const Block& Brain::findBestMatch(const std::vector<double>& target_fp,
                                  const SearchParams& params) {
    if (blocks_.empty()) {
        throw std::runtime_error("Brain::findBestMatch: brain is empty — add sounds first");
    }

    const std::size_t idx =
        search_->search(target_fp, blocks_, *analyser_, params, current_block_index_);
    current_block_index_ = idx;
    return blocks_[idx];
}

// ── Synapse graph ──────────────────────────────────────────────────────

void Brain::buildSynapses(const std::size_t num_synapses) {
    const std::size_t num_blocks = blocks_.size();
    const std::size_t k_synapses = std::min(num_synapses, num_blocks > 0 ? num_blocks - 1 : 0);

    for (std::size_t i = 0; i < num_blocks; ++i) {
        std::vector<std::pair<std::size_t, double>> scored;
        scored.reserve(num_blocks - 1);

        for (std::size_t j = 0; j < num_blocks; ++j) {
            if (j == i) {
                continue;
            }
            const double distance =
                analyser_->distance(blocks_[i].print.mfcc, blocks_[j].print.mfcc);
            scored.emplace_back(j, distance);
        }

        std::ranges::partial_sort(scored, scored.begin() + static_cast<std::ptrdiff_t>(k_synapses),
                                  [](const auto& a, const auto& b) { return a.second < b.second; });

        blocks_[i].synapses.clear();
        blocks_[i].synapses.reserve(k_synapses);
        for (auto s = 0; std::cmp_less(s, k_synapses); ++s) {
            blocks_[i].synapses.push_back(scored[s].first);
        }
    }
}

// ── Jiggle ─────────────────────────────────────────────────────────────

void Brain::jiggle() {
    if (!blocks_.empty()) {
        current_block_index_ = rng::randomIndex(blocks_.size());
    } else {
        current_block_index_ = 0;
    }
}

// ── Usage depletion ────────────────────────────────────────────────────

void Brain::depleteUsage(const double falloff) {
    if (falloff >= 1.0) {
        return;
    }
    for (auto& block : blocks_) {
        block.usage *= falloff;
    }
}

}  // namespace audio
