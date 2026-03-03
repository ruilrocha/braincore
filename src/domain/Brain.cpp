#include "Brain.h"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <utility>

#include "Random.h"
#include "WindowFunction.h"

namespace audio {

Brain::Brain(std::shared_ptr<port::IAnalyser>      analyser,
             std::shared_ptr<port::ISearchStrategy> search,
             BlockConfig config)
    : analyser_(std::move(analyser)),
      search_(std::move(search)),
      config_(config) {}

// ── Ingestion ──────────────────────────────────────────────────────────

void Brain::addSound(const Sound& sound, const std::string& name) {
    if (sound.getNumChannels() == 0) return;

    const Channel& ch0 = sound.getChannel(0);
    const int sample_rate = sound.getSampleRate();
    const auto bs = static_cast<std::size_t>(config_.block_size);
    const auto step = static_cast<std::size_t>(
        config_.block_size - config_.overlap);

    // Track source metadata.
    SourceSound src;
    src.filename = name;
    src.start    = blocks_.size();

    const int num_channels = sound.getNumChannels();

    for (std::size_t i = 0; i + bs <= ch0.size(); i += step) {
        Block block;
        block.source_name = name;

        // ── Extract mono samples (channel 0) for fingerprinting ────────
        std::vector<double> raw_samples(
            ch0.begin() + static_cast<std::ptrdiff_t>(i),
            ch0.begin() + static_cast<std::ptrdiff_t>(i + bs));

        // ── Extract multi-channel samples for reconstruction ───────────
        block.channel_samples.resize(num_channels);
        for (int ch = 0; ch < num_channels; ++ch) {
            const auto& src_ch = sound.getChannel(ch);
            block.channel_samples[ch].assign(
                src_ch.begin() + static_cast<std::ptrdiff_t>(i),
                src_ch.begin() + static_cast<std::ptrdiff_t>(i + bs));
        }

        // ── Apply window to raw samples before analysis ────────────────
        std::vector<double> windowed = raw_samples;
        WindowFunction::apply(windowed, config_.window);

        // ── Compute raw fingerprints via the generic analyse() port ────
        auto raw_fp = analyser_->analyse(windowed, sample_rate);
        block.fingerprint             = std::move(raw_fp.primary);
        block.secondary_fingerprint   = std::move(raw_fp.secondary);
        block.dominant_freq           = raw_fp.dominant_freq;

        // ── Compute normalised fingerprints ────────────────────────────
        std::vector<double> norm_samples = raw_samples;
        WindowFunction::normalise(norm_samples);
        WindowFunction::apply(norm_samples, config_.window);

        auto norm_fp = analyser_->analyse(norm_samples, sample_rate);
        block.normalised_fingerprint           = std::move(norm_fp.primary);
        block.normalised_secondary_fingerprint = std::move(norm_fp.secondary);

        // Store mono samples (windowed version is only for analysis).
        block.samples = std::move(raw_samples);

        blocks_.push_back(std::move(block));
    }

    src.end        = blocks_.size();
    src.num_blocks = src.end - src.start;
    sources_.push_back(std::move(src));
}

// ── Source management ──────────────────────────────────────────────────

void Brain::activateSound(const std::string& filename, bool active) {
    for (auto& s : sources_) {
        if (s.filename == filename) {
            s.enabled = active;
        }
    }
}

bool Brain::isBlockActive(const std::size_t index) const {
    return std::ranges::any_of(sources_, [index](const auto& s) {
        return index >= s.start && index < s.end && s.enabled;
    });
}

// ── Search ─────────────────────────────────────────────────────────────

const Block& Brain::findBestMatch(
    const std::vector<double>& target_fp,
    const SearchParams& params) {
    if (blocks_.empty()) {
        throw std::runtime_error(
            "Brain::findBestMatch: brain is empty — add sounds first");
    }

    const std::size_t idx =
        search_->search(target_fp, blocks_, *analyser_, params,
                        current_block_index_);
    current_block_index_ = idx;
    return blocks_[idx];
}

// ── Synapse graph ──────────────────────────────────────────────────────

void Brain::buildSynapses(const std::size_t num_synapses) {
    const std::size_t n = blocks_.size();
    const std::size_t k = std::min(num_synapses, n > 0 ? n - 1 : 0);

    for (std::size_t i = 0; i < n; ++i) {
        std::vector<std::pair<std::size_t, double>> scored;
        scored.reserve(n - 1);

        for (std::size_t j = 0; j < n; ++j) {
            if (j == i) continue;
            const double d = analyser_->distance(
                blocks_[i].fingerprint, blocks_[j].fingerprint);
            scored.emplace_back(j, d);
        }

        std::ranges::partial_sort(scored, scored.begin() + static_cast<std::ptrdiff_t>(k),
                                  [](const auto& a, const auto& b) {
                                      return a.second < b.second;
                                  });

        blocks_[i].synapses.clear();
        blocks_[i].synapses.reserve(k);
        for (std::size_t s = 0; s < k; ++s) {
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
    if (falloff >= 1.0) return;
    for (auto& b : blocks_) {
        b.usage *= falloff;
    }
}

} // namespace audio
