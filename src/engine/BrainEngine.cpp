#include "BrainEngine.h"

#include "../adapter/analysis/MfccAnalyser.h"
#include "../adapter/effects/SpectralMorph.h"
#include "../adapter/fft/PocketfftBackend.h"
#include "../adapter/search/ClosestSearch.h"
#include "../adapter/search/SynapticSearch.h"
#include "../adapter/search/VpTreeSearch.h"
#include "../domain/BlockAnalysis.h"
#include "../domain/BlockConfig.h"
#include "../domain/Brain.h"
#include "../domain/PlayHead.h"
#include "../domain/SearchParams.h"
#include "../domain/constants.h"
#include "AtomicSearchParams.h"
#include "InfiniteDriftState.h"
#include "OutputPipeline.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace audio {

// ── Impl ──────────────────────────────────────────────────────────────────────

struct BrainEngine::Impl {
    // ── Config (requires re-prepare when changed) ─────────────────────────
    int block_size = kDefaultBlockSize;
    WindowShape window_shape = WindowShape::Rectangle;
    double overlap_ratio = 0.5;
    std::size_t num_synapses = kDefaultNumSynapses;
    SearchStrategy strategy = SearchStrategy::VpTree;

    // ── Thread-safe search params ──────────────────────────────────────────
    AtomicSearchParams params;

    // ── Core domain objects ───────────────────────────────────────────────
    std::shared_ptr<Brain> brain;
    std::optional<PlayHead> play_head;

    // ── Per-block source metadata ─────────────────────────────────────────
    struct BlockMeta {
        std::string source_name;
        double time_offset_sec = 0.0;
    };
    std::vector<BlockMeta> block_meta;

    // ── Infinite mode drift state ──────────────────────────────────────────
    InfiniteDriftState drift;

    // ── Output pipeline (OLA + BlockEffectChain) ──────────────────────────
    // Constructed lazily in resetBrain() once config is known.
    std::optional<OutputPipeline> output;

    // ── Scratch buffer (avoid heap alloc on audio thread) ─────────────────
    std::vector<double> advance_scratch;

    Impl() = default;

    void resetBrain() {
        auto fft = std::make_shared<adapter::fft::PocketfftBackend>();
        auto analyser = std::make_shared<adapter::analysis::MfccAnalyser>(std::move(fft));
        BlockConfig cfg;
        cfg.overlap = overlap_ratio;
        cfg.block_size = block_size;
        cfg.window = window_shape;
        brain = std::make_shared<Brain>(std::move(analyser), cfg);
        play_head.reset();
        block_meta.clear();
        drift.reset();
        output.emplace(block_size, overlap_ratio, window_shape);
    }

    void clear() {
        brain.reset();
        play_head.reset();
        block_meta.clear();
        drift.reset();
        if (output) {
            output->reset();
        }
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

    [[nodiscard]] std::size_t stepSamples() const noexcept {
        const auto bs = static_cast<std::size_t>(block_size);
        if (overlap_ratio <= 0.0) {
            return bs;
        }
        return std::max(std::size_t{1},
                        static_cast<std::size_t>(static_cast<double>(bs) * (1.0 - overlap_ratio)));
    }
};

// ── Construction ──────────────────────────────────────────────────────────────

BrainEngine::BrainEngine() : impl_(std::make_unique<Impl>()) {}
BrainEngine::~BrainEngine() = default;
BrainEngine::BrainEngine(BrainEngine&&) noexcept = default;
BrainEngine& BrainEngine::operator=(BrainEngine&&) noexcept = default;

// ── Brain config ──────────────────────────────────────────────────────────────

void BrainEngine::setBlockSize(const int block_size) noexcept {
    impl_->block_size = block_size;
}
int BrainEngine::getBlockSize() const noexcept {
    return impl_->block_size;
}

void BrainEngine::setWindowShape(const WindowShape shape) noexcept {
    impl_->window_shape = shape;
}
WindowShape BrainEngine::getWindowShape() const noexcept {
    return impl_->window_shape;
}

void BrainEngine::setOverlapRatio(const double ratio) noexcept {
    impl_->overlap_ratio = std::max(0.0, std::min(ratio, 0.9));
}
double BrainEngine::getOverlapRatio() const noexcept {
    return impl_->overlap_ratio;
}

void BrainEngine::setNumSynapses(const std::size_t n) noexcept {
    impl_->num_synapses = n;
}
std::size_t BrainEngine::getNumSynapses() const noexcept {
    return impl_->num_synapses;
}

// ── Search strategy ───────────────────────────────────────────────────────────

void BrainEngine::setSearchStrategy(const SearchStrategy strategy) {
    if (impl_->strategy == strategy) {
        return;
    }
    impl_->strategy = strategy;
    if (impl_->play_head.has_value()) {
        impl_->buildPlayHead();
    }
}
SearchStrategy BrainEngine::searchStrategy() const noexcept {
    return impl_->strategy;
}

// ── Search params ─────────────────────────────────────────────────────────────

void BrainEngine::setUsageWeight(const double val) noexcept {
    impl_->params.usage_weight.store(val, std::memory_order_relaxed);
}
void BrainEngine::setUsageFalloff(const double val) noexcept {
    impl_->params.usage_falloff.store(val, std::memory_order_relaxed);
}
void BrainEngine::setStickyness(const double val) noexcept {
    impl_->params.stickyness.store(val, std::memory_order_relaxed);
}
void BrainEngine::setMfccWeight(const double val) noexcept {
    impl_->params.mfcc_weight.store(val, std::memory_order_relaxed);
}
void BrainEngine::setMelWeight(const double val) noexcept {
    impl_->params.mel_weight.store(val, std::memory_order_relaxed);
}
void BrainEngine::setSpectralWeight(const double val) noexcept {
    impl_->params.spectral_weight.store(val, std::memory_order_relaxed);
}
void BrainEngine::setNRatio(const double val) noexcept {
    impl_->params.n_ratio.store(val, std::memory_order_relaxed);
}
void BrainEngine::setBrightnessTarget(const double val) noexcept {
    impl_->params.brightness_target.store(val, std::memory_order_relaxed);
}
void BrainEngine::setBrightnessWeight(const double val) noexcept {
    impl_->params.brightness_weight.store(val, std::memory_order_relaxed);
}

// ── Effects ───────────────────────────────────────────────────────────────────

void BrainEngine::addEffect(const EffectType type) {
    if (!impl_->output) {
        return;
    }
    if (type == EffectType::SpectralMorph) {
        auto fft = std::make_shared<adapter::fft::PocketfftBackend>();
        auto morph = std::make_shared<adapter::effects::SpectralMorph>(std::move(fft));
        impl_->output->addEffect(type, std::move(morph));
    }
}

void BrainEngine::removeEffect(const EffectType type) {
    if (impl_->output) {
        impl_->output->removeEffect(type);
    }
}

void BrainEngine::setEffectAmount(const EffectType type, const double amount) noexcept {
    if (impl_->output) {
        impl_->output->setEffectAmount(type, amount);
    }
}

// ── Ingestion ─────────────────────────────────────────────────────────────────

void BrainEngine::addSound(const Sound& sound, const std::string& name) {
    if (sound.getChannels().empty() || sound.getChannels()[0].empty()) {
        return;
    }
    if (!impl_->brain) {
        impl_->resetBrain();
    }
    const std::size_t first_block = impl_->brain->size();
    impl_->brain->addSound(sound, name);

    const std::size_t step = impl_->stepSamples();
    std::size_t pos = 0;
    for (std::size_t i = first_block; i < impl_->brain->size(); ++i, pos += step) {
        impl_->block_meta.push_back(
            {.source_name = name,
             .time_offset_sec =
                 static_cast<double>(pos) / static_cast<double>(sound.getSampleRate())});
    }
}

void BrainEngine::buildIndex() {
    if (!impl_->brain) {
        return;
    }
    impl_->brain->buildIndex(impl_->num_synapses);
    impl_->buildPlayHead();
}

void BrainEngine::clear() noexcept {
    impl_->clear();
}

void BrainEngine::resetPlayback() noexcept {
    if (impl_->play_head) {
        impl_->play_head->reset();
    }
    impl_->drift.reset();
    if (impl_->output) {
        impl_->output->reset();
    }
}

bool BrainEngine::hasBrain() const noexcept {
    return static_cast<bool>(impl_->brain);
}

// ── Playback ──────────────────────────────────────────────────────────────────

std::size_t BrainEngine::advance(const std::vector<double>& samples, const int sample_rate) {
    if (!impl_->brain || samples.empty()) {
        return 0;
    }
    if (!impl_->play_head.has_value()) {
        impl_->buildPlayHead();
    }
    if (!impl_->play_head.has_value()) {
        return 0;
    }

    impl_->advance_scratch.assign(samples.begin(), samples.end());
    const AudioPrint print = impl_->brain->analyser().analyse(impl_->advance_scratch, sample_rate);
    const BlockAnalysis target{.print = print, .normalised_print = print};
    const std::size_t matched = impl_->play_head->advance(target, impl_->params.snapshot());

    if (impl_->output) {
        impl_->output->push(matched, impl_->brain->blocks()[matched].channel_samples);
    }
    return matched;
}

std::size_t BrainEngine::advanceInfinite(const int /*sample_rate*/) {
    if (!impl_->brain || impl_->brain->empty()) {
        return 0;
    }
    if (!impl_->play_head.has_value()) {
        impl_->buildPlayHead();
    }

    if (!impl_->drift.seeded()) {
        impl_->drift.initFromNoise(*impl_->brain);
    }

    SearchParams sp = impl_->params.snapshot();
    sp.usage_weight = std::max(sp.usage_weight, 0.002);
    sp.usage_falloff = std::min(sp.usage_falloff, 0.92);

    if (!impl_->play_head.has_value()) {
        return 0;
    }
    const std::size_t matched = impl_->play_head->advance(impl_->drift.currentTarget(), sp);

    impl_->drift.updateFromMatch(impl_->brain->blocks()[matched].analysis.print, matched,
                                 *impl_->brain, impl_->play_head->blockUsages(), sp);

    if (impl_->output) {
        impl_->output->push(matched, impl_->brain->blocks()[matched].channel_samples);
    }
    return matched;
}

// ── Block data ────────────────────────────────────────────────────────────────

std::size_t BrainEngine::blockCount() const noexcept {
    return impl_->brain ? impl_->brain->size() : 0;
}

std::size_t BrainEngine::blockSize() const noexcept {
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

std::size_t BrainEngine::stepSize() const noexcept {
    if (impl_->output && impl_->output->olaActive()) {
        return impl_->output->stepSize();
    }
    return blockSize();
}

int BrainEngine::blockChannels(const std::size_t index) const noexcept {
    if (!impl_->brain) {
        return 0;
    }
    const auto& blocks = impl_->brain->blocks();
    if (index >= blocks.size()) {
        return 0;
    }
    return static_cast<int>(blocks[index].channel_samples.size());
}

std::size_t BrainEngine::getBlockSamples(const std::size_t index, double* out_buffer,
                                         const std::size_t max_count) const {
    if (out_buffer == nullptr || !impl_->brain || !impl_->output) {
        return 0;
    }
    return impl_->output->readMono(index, impl_->brain->blocks(), out_buffer, max_count);
}

std::size_t BrainEngine::getBlockSamplesInterleaved(const std::size_t index, double* out_buffer,
                                                    const std::size_t max_frames) const {
    if (out_buffer == nullptr || !impl_->brain || !impl_->output) {
        return 0;
    }
    return impl_->output->readInterleaved(index, impl_->brain->blocks(), out_buffer, max_frames);
}

// ── Block source metadata ─────────────────────────────────────────────────────

std::string BrainEngine::getBlockSourceName(const std::size_t index) const {
    if (index >= impl_->block_meta.size()) {
        return {};
    }
    return impl_->block_meta[index].source_name;
}

double BrainEngine::getBlockTimeOffset(const std::size_t index) const noexcept {
    if (index >= impl_->block_meta.size()) {
        return -1.0;
    }
    return impl_->block_meta[index].time_offset_sec;
}

}  // namespace audio
