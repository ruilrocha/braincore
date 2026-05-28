#include "BrainSession.h"

#include "adapter/analysis/MfccAnalyser.h"
#include "adapter/fft/PocketfftBackend.h"
#include "adapter/search/ClosestSearch.h"
#include "adapter/search/SynapticSearch.h"
#include "adapter/search/VpTreeSearch.h"
#include "domain/AudioPrint.h"
#include "domain/BlockConfig.h"
#include "domain/Brain.h"
#include "domain/PlayHead.h"
#include "domain/Random.h"
#include "domain/SearchParams.h"
#include "domain/Sound.h"
#include "domain/constants.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace audio {

// ── Pimpl ─────────────────────────────────────────────────────────────────────

struct BrainSession::Impl {
    // Brain config (requires re-prepare when changed)
    int block_size = kDefaultBlockSize;
    WindowShape window_shape = WindowShape::Rectangle;
    std::size_t num_synapses = kDefaultNumSynapses;

    // Search config (cheap — only rebuilds PlayHead)
    SearchStrategy strategy = SearchStrategy::VpTree;

    // Live search parameters — used by advance() and advanceInfinite()
    SearchParams search_params;

    std::shared_ptr<Brain> brain;
    std::optional<PlayHead> play_head;

    // Per-block source tracking for video mapping: (source_name, time_offset_sec)
    struct BlockMeta {
        std::string source_name;
        double time_offset_sec = 0.0;
    };
    std::vector<BlockMeta> block_meta;

    // Infinite mode drift state
    AudioPrint drift_print;          ///< Evolving synthetic "target" fingerprint.
    std::size_t drift_last_idx = 0;  ///< Last matched index, for stuck detection.
    int drift_stuck_count = 0;       ///< How many steps the same block was returned.

    Impl() = default;  // Brain created lazily in addSamples so config is applied first

    void resetBrain() {
        auto fft = std::make_shared<adapter::fft::PocketfftBackend>();
        auto analyser = std::make_shared<adapter::analysis::MfccAnalyser>(std::move(fft));
        BlockConfig cfg;
        cfg.block_size = block_size;
        cfg.window = window_shape;
        brain = std::make_shared<Brain>(std::move(analyser), cfg);
        play_head.reset();
        block_meta.clear();
        drift_print = {};
        drift_last_idx = 0;
        drift_stuck_count = 0;
    }

    /** Called from Swift reset() — clears sounds but preserves config. */
    void clear() {
        brain.reset();
        play_head.reset();
        block_meta.clear();
        drift_print = {};
        drift_last_idx = 0;
        drift_stuck_count = 0;
    }

    /** Seed drift_print from a randomly chosen real block in the brain.
     *  Starting in actual timbral space avoids silent/near-zero blocks that
     *  random noise tends to match.
     */
    void initDriftFromNoise() {
        if (!brain || brain->empty()) {
            return;
        }
        const std::size_t seed_idx = rng::randomIndex(brain->blocks().size());
        const AudioPrint& ref = brain->blocks()[seed_idx].print;
        auto copyWithNoise = [](const std::vector<double>& src) {
            std::vector<double> vec(src.size());
            for (std::size_t i = 0; i < src.size(); ++i) {
                vec[i] = src[i] + rng::randomDouble(-0.1, 0.1);
            }
            return vec;
        };
        drift_print.mfcc = copyWithNoise(ref.mfcc);
        drift_print.mel = copyWithNoise(ref.mel);
        drift_print.spectral = copyWithNoise(ref.spectral);
        drift_print.chroma = copyWithNoise(ref.chroma);
    }

    [[nodiscard]] std::shared_ptr<port::ISearchStrategy> makeStrategy() const {
        switch (strategy) {
            case SearchStrategy::Closest:
                return std::make_shared<adapter::search::ClosestSearch>();
            case SearchStrategy::VpTree:
                return std::make_shared<adapter::search::VpTreeSearch>();
            case SearchStrategy::Synaptic:
                return std::make_shared<adapter::search::SynapticSearch>();
        }
        return std::make_shared<adapter::search::VpTreeSearch>();
    }

    void buildPlayHead() {
        auto const_brain = std::const_pointer_cast<const Brain>(brain);
        play_head.emplace(std::move(const_brain), makeStrategy());
    }
};

// ── Construction ──────────────────────────────────────────────────────────────

BrainSession::BrainSession() : impl_(std::make_unique<Impl>()) {}
BrainSession::~BrainSession() = default;
BrainSession::BrainSession(BrainSession&&) noexcept = default;
BrainSession& BrainSession::operator=(BrainSession&&) noexcept = default;

// ── Brain config ──────────────────────────────────────────────────────────────

void BrainSession::setBlockSize(const int block_size) const noexcept {
    impl_->block_size = block_size;
}
int BrainSession::getBlockSize() const noexcept {
    return impl_->block_size;
}

void BrainSession::setWindowShape(const WindowShape shape) const noexcept {
    impl_->window_shape = shape;
}
WindowShape BrainSession::getWindowShape() const noexcept {
    return impl_->window_shape;
}

void BrainSession::setNumSynapses(const std::size_t n) const noexcept {
    impl_->num_synapses = n;
}
std::size_t BrainSession::getNumSynapses() const noexcept {
    return impl_->num_synapses;
}

// ── Search strategy ───────────────────────────────────────────────────────────

void BrainSession::setSearchStrategy(const SearchStrategy strategy) const {
    impl_->strategy = strategy;
    if (impl_->play_head.has_value()) {
        impl_->buildPlayHead();
    }
}
SearchStrategy BrainSession::searchStrategy() const noexcept {
    return impl_->strategy;
}

// ── Search params ─────────────────────────────────────────────────────────────

void BrainSession::setUsageWeight(const double val) noexcept {
    impl_->search_params.usage_weight = val;
}
void BrainSession::setUsageFalloff(const double val) noexcept {
    impl_->search_params.usage_falloff = val;
}
void BrainSession::setStickyness(const double val) noexcept {
    impl_->search_params.stickyness = val;
}
void BrainSession::setMfccWeight(const double val) noexcept {
    impl_->search_params.mfcc_weight = val;
}
void BrainSession::setMelWeight(const double val) noexcept {
    impl_->search_params.mel_weight = val;
}
void BrainSession::setSpectralWeight(const double val) noexcept {
    impl_->search_params.spectral_weight = val;
}
void BrainSession::setNRatio(const double val) noexcept {
    impl_->search_params.n_ratio = val;
}

// ── Clear ────────────────────────────────────────────────────────────────────

void BrainSession::clear() const noexcept {
    impl_->clear();
}

// ── Ingestion ─────────────────────────────────────────────────────────────────

void BrainSession::addSamples(const double* samples, const std::size_t count, const int sample_rate,
                              const char* name) {
    if (!impl_->brain) {
        impl_->resetBrain();
    }
    const std::string label = (name != nullptr) ? name : "";
    const std::size_t first_block = impl_->brain->size();

    std::vector<double> vec(samples, samples + count);
    Sound sound({std::move(vec)}, sample_rate);
    impl_->brain->addSound(sound, label);

    // Record per-block metadata for Swift video mapping
    const auto bs = static_cast<std::size_t>(impl_->block_size);
    const std::size_t step = bs;  // overlap=0 for now
    std::size_t pos = 0;
    for (std::size_t i = first_block; i < impl_->brain->size(); ++i, pos += step) {
        impl_->block_meta.push_back(
            {.source_name = label,
             .time_offset_sec = static_cast<double>(pos) / static_cast<double>(sample_rate)});
    }
}

void BrainSession::addSamplesInterleaved(const double* samples, const std::size_t frame_count,
                                         const int channels, const int sample_rate,
                                         const char* name) {
    if (channels <= 0 || frame_count == 0) {
        return;
    }
    if (!impl_->brain) {
        impl_->resetBrain();
    }
    const std::string label = (name != nullptr) ? name : "";
    const std::size_t first_block = impl_->brain->size();

    std::vector<std::vector<double>> ch_data(channels, std::vector<double>(frame_count));
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        for (int ch = 0; ch < channels; ++ch) {
            ch_data[ch][frame] = samples[(frame * static_cast<std::size_t>(channels)) + ch];
        }
    }
    Sound sound(std::move(ch_data), sample_rate);
    impl_->brain->addSound(sound, label);

    const auto bs = static_cast<std::size_t>(impl_->block_size);
    const std::size_t step = bs;
    std::size_t pos = 0;
    for (std::size_t i = first_block; i < impl_->brain->size(); ++i, pos += step) {
        impl_->block_meta.push_back(
            {.source_name = label,
             .time_offset_sec = static_cast<double>(pos) / static_cast<double>(sample_rate)});
    }
}

void BrainSession::buildIndex() {
    impl_->brain->buildIndex(impl_->num_synapses);
    impl_->buildPlayHead();
}

// ── Playback ──────────────────────────────────────────────────────────────────

std::size_t BrainSession::advance(const double* samples, const std::size_t count,
                                  const int sample_rate) {
    if (!impl_->brain) {
        return 0;
    }
    if (!impl_->play_head.has_value()) {
        impl_->buildPlayHead();
    }
    const std::vector<double> vec(samples, samples + count);
    const AudioPrint print = impl_->brain->analyser().analyse(vec, sample_rate);
    const TargetAnalysis target{.print = print, .normalised_print = print};
    const std::size_t matched = impl_->play_head.value().advance(target, impl_->search_params);
    impl_->play_head.value().depleteUsages(impl_->search_params.usage_falloff);
    return matched;
}

std::size_t BrainSession::advanceInfinite(const int /*sample_rate*/) {
    if (!impl_->brain || impl_->brain->empty()) {
        return 0;
    }
    if (!impl_->play_head.has_value()) {
        impl_->buildPlayHead();
    }

    // Seed the drift fingerprint with noise on first call — every session starts
    // from a different random position in timbral space.
    if (impl_->drift_print.mfcc.empty()) {
        impl_->initDriftFromNoise();
    }

    // In infinite mode always penalise the selected block so it can't win again
    // immediately — this is the core mechanism that forces the path forward.
    SearchParams infinite_params = impl_->search_params;
    infinite_params.usage_weight = std::max(impl_->search_params.usage_weight, 0.002);
    infinite_params.usage_falloff = std::min(impl_->search_params.usage_falloff, 0.92);

    // Search: find the block most similar to the current target.
    const TargetAnalysis target{.print = impl_->drift_print,
                                .normalised_print = impl_->drift_print};
    const std::size_t matched = impl_->play_head.value().advance(target, infinite_params);
    impl_->play_head.value().depleteUsages(infinite_params.usage_falloff);

    // The matched block becomes the next target — this traces a path through
    // timbral space where each block leads toward its most similar neighbour.
    // A tiny noise perturbation prevents the path from being fully deterministic.
    // If the matched block is silent (near-zero energy), jump to a fresh random
    // starting point rather than letting the path get trapped in silence.
    const AudioPrint& mp = impl_->brain->blocks()[matched].print;
    const double energy = [&] {
        double sum = 0.0;
        for (const double v : mp.mel) {
            sum += v * v;
        }
        return sum;
    }();

    if (energy < 1e-6) {
        impl_->initDriftFromNoise();
    } else {
        auto copyWithNoise = [](std::vector<double>& dst, const std::vector<double>& src) {
            dst.resize(src.size());
            for (std::size_t i = 0; i < src.size(); ++i) {
                dst[i] = src[i] + rng::randomDouble(-0.05, 0.05);
            }
        };
        copyWithNoise(impl_->drift_print.mfcc, mp.mfcc);
        copyWithNoise(impl_->drift_print.mel, mp.mel);
        copyWithNoise(impl_->drift_print.spectral, mp.spectral);
    }

    // Safety net: if somehow stuck (tiny brain, usage params at 0), reinit from noise.
    if (matched == impl_->drift_last_idx) {
        ++impl_->drift_stuck_count;
        if (impl_->drift_stuck_count > 16) {
            impl_->initDriftFromNoise();
            impl_->drift_stuck_count = 0;
        }
    } else {
        impl_->drift_stuck_count = 0;
        impl_->drift_last_idx = matched;
    }

    return matched;
}

// ── Block data ────────────────────────────────────────────────────────────────

std::size_t BrainSession::blockCount() const noexcept {
    return impl_->brain ? impl_->brain->size() : 0;
}

std::size_t BrainSession::blockSize() const noexcept {
    if (!impl_->brain) {
        return static_cast<std::size_t>(impl_->block_size);
    }
    const auto& blocks = impl_->brain->blocks();
    if (blocks.empty()) {
        return 0;
    }
    const auto& ch = blocks[0].channel_samples;
    return ch.empty() ? 0 : ch[0].size();
}

int BrainSession::blockChannels(const std::size_t index) const noexcept {
    if (!impl_->brain) {
        return 0;
    }
    const auto& blocks = impl_->brain->blocks();
    if (index >= blocks.size()) {
        return 0;
    }
    return static_cast<int>(blocks[index].channel_samples.size());
}

std::size_t BrainSession::getBlockSamples(const std::size_t index, double* out_buffer,
                                          const std::size_t max_count) const noexcept {
    if ((out_buffer == nullptr) || !impl_->brain) {
        return 0;
    }
    const auto& blocks = impl_->brain->blocks();
    if (index >= blocks.size()) {
        return 0;
    }
    const auto& ch = blocks[index].channel_samples;
    if (ch.empty()) {
        return 0;
    }
    const auto& src = ch[0];
    const std::size_t n = std::min(src.size(), max_count);
    std::copy_n(src.begin(), static_cast<std::ptrdiff_t>(n), out_buffer);
    return n;
}

std::size_t BrainSession::getBlockSamplesInterleaved(const std::size_t index, double* out_buffer,
                                                     const std::size_t max_frames) const noexcept {
    if ((out_buffer == nullptr) || !impl_->brain) {
        return 0;
    }
    const auto& blocks = impl_->brain->blocks();
    if (index >= blocks.size()) {
        return 0;
    }
    const auto& channels = blocks[index].channel_samples;
    if (channels.empty()) {
        return 0;
    }
    const std::size_t frames = std::min(channels[0].size(), max_frames);
    const std::size_t nch = channels.size();
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t chi = 0; chi < nch; ++chi) {
            out_buffer[(frame * nch) + chi] = channels[chi][frame];
        }
    }
    return frames;
}

// ── Block source metadata ─────────────────────────────────────────────────────

std::string BrainSession::getBlockSourceName(const std::size_t index) const noexcept {
    if (index >= impl_->block_meta.size()) {
        return {};
    }
    return impl_->block_meta[index].source_name;
}

double BrainSession::getBlockTimeOffset(const std::size_t index) const noexcept {
    if (index >= impl_->block_meta.size()) {
        return -1.0;
    }
    return impl_->block_meta[index].time_offset_sec;
}

// ── Diagnostics ───────────────────────────────────────────────────────────────

std::string BrainSession::selfTest() const {
    if (!impl_->brain) {
        return "BrainSession::selfTest: brain not initialised.";
    }
    const auto& blocks = impl_->brain->blocks();
    if (blocks.empty()) {
        return "BrainSession::selfTest: brain is empty.";
    }

    const auto& first_fp = blocks[0].print.mfcc;
    std::size_t best = 0;
    double best_dist = std::numeric_limits<double>::max();
    for (std::size_t i = 1; i < blocks.size(); ++i) {
        const double dist = impl_->brain->analyser().distance(first_fp, blocks[i].print.mfcc);
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }

    const auto* strat_name = "unknown";
    switch (impl_->strategy) {
        case SearchStrategy::Closest:
            strat_name = "Closest";
            break;
        case SearchStrategy::VpTree:
            strat_name = "VpTree";
            break;
        case SearchStrategy::Synaptic:
            strat_name = "Synaptic";
            break;
    }
    const char* win_name = "unknown";
    switch (impl_->window_shape) {
        case WindowShape::Rectangle:
            win_name = "Rectangle";
            break;
        case WindowShape::Hamming:
            win_name = "Hamming";
            break;
        case WindowShape::Hann:
            win_name = "Hann";
            break;
        case WindowShape::Blackman:
            win_name = "Blackman";
            break;
        case WindowShape::Bartlett:
            win_name = "Bartlett";
            break;
        case WindowShape::FlatTop:
            win_name = "FlatTop";
            break;
        case WindowShape::Gaussian:
            win_name = "Gaussian";
            break;
    }

    std::ostringstream ss;
    ss << "BrainSession selfTest:\n"
       << "  blocks        : " << blocks.size() << "\n"
       << "  block size    : " << impl_->block_size << "\n"
       << "  window        : " << win_name << "\n"
       << "  num_synapses  : " << impl_->num_synapses << "\n"
       << "  strategy      : " << strat_name << "\n"
       << "  index built   : " << (impl_->brain->index() != nullptr ? "yes" : "no") << "\n"
       << "  nearest to [0]: block " << best << " (dist=" << best_dist << ")\n";
    return ss.str();
}

}  // namespace audio
