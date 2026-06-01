#include "BrainSession.h"

// NOLINTBEGIN(readability-make-member-function-const)
// BrainSession uses the Pimpl idiom: all methods access state through a
// unique_ptr<Impl>, so the pointer itself never changes — the compiler
// considers every method "can be const". Suppressed here only; internal
// domain classes are still checked.

#include "adapter/analysis/MfccAnalyser.h"
#include "adapter/effects/OlaBuffer.h"
#include "adapter/effects/SpectralMorph.h"
#include "adapter/fft/PocketfftBackend.h"
#include "adapter/search/ClosestSearch.h"
#include "adapter/search/SynapticSearch.h"
#include "adapter/search/VpTreeSearch.h"
#include "domain/BlockAnalysis.h"
#include "domain/BlockConfig.h"
#include "domain/Brain.h"
#include "domain/PlayHead.h"
#include "domain/Random.h"
#include "domain/SearchParams.h"
#include "domain/Sound.h"
#include "domain/constants.h"

#include <algorithm>
#include <atomic>
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
    double overlap_ratio = 0.5;  ///< OLA overlap [0.0, 0.9]. 0 = no OLA; 0.5 = 50% (recommended).
    std::size_t num_synapses = kDefaultNumSynapses;

    // Search config (cheap — only rebuilds PlayHead)
    SearchStrategy strategy = SearchStrategy::VpTree;

    // Live search parameters — written from UI/main thread, read from the audio thread.
    // Atomics with relaxed ordering: a stale-by-one-block read is acceptable for audio params.
    std::atomic<double> a_stickyness{0.0};
    std::atomic<double> a_usage_weight{0.0};
    std::atomic<double> a_usage_falloff{1.0};
    std::atomic<double> a_mfcc_weight{0.0};
    std::atomic<double> a_mel_weight{1.0};
    std::atomic<double> a_spectral_weight{0.0};
    std::atomic<double> a_n_ratio{0.0};

    /// Snapshot all realtime atomics into a SearchParams for one audio block.
    [[nodiscard]] SearchParams snapshotParams() const noexcept {
        SearchParams sp;
        sp.stickyness = a_stickyness.load(std::memory_order_relaxed);
        sp.usage_weight = a_usage_weight.load(std::memory_order_relaxed);
        sp.usage_falloff = a_usage_falloff.load(std::memory_order_relaxed);
        sp.mfcc_weight = a_mfcc_weight.load(std::memory_order_relaxed);
        sp.mel_weight = a_mel_weight.load(std::memory_order_relaxed);
        sp.spectral_weight = a_spectral_weight.load(std::memory_order_relaxed);
        sp.n_ratio = a_n_ratio.load(std::memory_order_relaxed);
        return sp;
    }

    std::shared_ptr<Brain> brain;
    std::optional<PlayHead> play_head;

    // Per-block source tracking for video mapping: (source_name, time_offset_sec)
    struct BlockMeta {
        std::string source_name;
        double time_offset_sec = 0.0;
    };
    std::vector<BlockMeta> block_meta;

    // Infinite mode drift state
    BlockAnalysis drift_print;       ///< Evolving "target" fingerprint for path walking.
    std::size_t drift_last_idx = 0;  ///< Last matched index, for stuck detection.
    int drift_stuck_count = 0;       ///< How many steps the same block was returned.

    // Effect pipeline
    std::unique_ptr<adapter::effects::SpectralMorph> spectral_morph;
    std::atomic<double> a_spectral_morph_amount{0.0};
    std::optional<std::size_t> prev_matched_idx;  ///< Block index from the previous advance() call.
    std::optional<std::size_t>
        last_matched_idx;  ///< Block index from the most recent advance() call.
    /// Feedback cache: last morphed output per channel. Fed back as `prev` on the
    /// next block so the morph acts as a spectral IIR filter (reverb-like smearing).
    std::vector<std::vector<double>> morph_feedback;

    /// OLA synthesis buffer — active when BlockConfig.overlap > 0.
    /// Created/replaced in resetBrain() with the current window_shape.
    std::unique_ptr<adapter::effects::OlaBuffer> ola_buffer;
    /// Scratch buffer reused by getBlockSamplesInterleaved() for OLA reads.
    std::vector<std::vector<double>> ola_read_scratch;

    Impl() = default;  // Brain created lazily in addSamples so config is applied first

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
        drift_print = {};
        drift_last_idx = 0;
        drift_stuck_count = 0;
        prev_matched_idx.reset();
        last_matched_idx.reset();
        morph_feedback.clear();

        // OLA synthesis buffer — uses the user's selected window shape.
        ola_buffer = std::make_unique<adapter::effects::OlaBuffer>(
            static_cast<std::size_t>(block_size), cfg.overlap, window_shape);
        ola_read_scratch.clear();
    }

    /** Called from Swift reset() — clears sounds but preserves config. */
    void clear() {
        brain.reset();
        play_head.reset();
        block_meta.clear();
        drift_print = {};
        drift_last_idx = 0;
        drift_stuck_count = 0;
        prev_matched_idx.reset();
        last_matched_idx.reset();
        morph_feedback.clear();
        if (ola_buffer) {
            ola_buffer->resetBuffer();
        }
    }

    void initDriftFromNoise() {
        if (!brain || brain->empty()) {
            return;
        }
        const std::size_t seed_idx = rng::randomIndex(brain->blocks().size());
        const AudioPrint& ref = brain->blocks()[seed_idx].analysis.print;
        auto copyWithNoise = [](const std::vector<float>& src) {
            std::vector<float> vec(src.size());
            for (std::size_t i = 0; i < src.size(); ++i) {
                vec[i] = src[i] + static_cast<float>(rng::randomDouble(-0.1, 0.1));
            }
            return vec;
        };
        drift_print.print.mfcc = copyWithNoise(ref.mfcc);
        drift_print.print.mel = copyWithNoise(ref.mel);
        drift_print.print.spectral = copyWithNoise(ref.spectral);
        drift_print.print.chroma = copyWithNoise(ref.chroma);
        drift_print.normalised_print = drift_print.print;
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

    /// Step size in samples — mirrors Brain::addSound's step calculation.
    /// Both Brain::addSound and block_meta must use the same value.
    [[nodiscard]] std::size_t stepSamples() const noexcept {
        const auto bs = static_cast<std::size_t>(block_size);
        if (overlap_ratio <= 0.0) {
            return bs;
        }
        return std::max(std::size_t{1},
                        static_cast<std::size_t>(static_cast<double>(bs) * (1.0 - overlap_ratio)));
    }

    /// Pre-allocated scratch for advance() — avoids heap allocation on the audio thread.
    std::vector<double> advance_scratch;
    /// Pre-allocated float→double scratch for SpectralMorph input — avoids heap allocation
    /// on the audio thread when widening float channel_samples to the double morph API.
    std::vector<double> float_to_double_scratch;
};

// ── Construction ──────────────────────────────────────────────────────────────

BrainSession::BrainSession() : impl_(std::make_unique<Impl>()) {}
BrainSession::~BrainSession() = default;
BrainSession::BrainSession(BrainSession&&) noexcept = default;
BrainSession& BrainSession::operator=(BrainSession&&) noexcept = default;

// ── Brain config ──────────────────────────────────────────────────────────────

void BrainSession::setBlockSize(const int block_size) noexcept {
    impl_->block_size = block_size;
}
int BrainSession::getBlockSize() const noexcept {
    return impl_->block_size;
}

void BrainSession::setWindowShape(const WindowShape shape) noexcept {
    impl_->window_shape = shape;
}
WindowShape BrainSession::getWindowShape() const noexcept {
    return impl_->window_shape;
}

void BrainSession::setOverlapRatio(const double ratio) noexcept {
    impl_->overlap_ratio = std::max(0.0, std::min(ratio, 0.9));
}
double BrainSession::getOverlapRatio() const noexcept {
    return impl_->overlap_ratio;
}

void BrainSession::setNumSynapses(const std::size_t n) noexcept {
    impl_->num_synapses = n;
}
std::size_t BrainSession::getNumSynapses() const noexcept {
    return impl_->num_synapses;
}

// ── Search strategy ───────────────────────────────────────────────────────────

void BrainSession::setSearchStrategy(const SearchStrategy strategy) {
    if (impl_->strategy == strategy) {
        return;  // no-op — avoids resetting current_block_idx_ on every streaming block
    }
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
    impl_->a_usage_weight.store(val, std::memory_order_relaxed);
}
void BrainSession::setUsageFalloff(const double val) noexcept {
    impl_->a_usage_falloff.store(val, std::memory_order_relaxed);
}
void BrainSession::setStickyness(const double val) noexcept {
    impl_->a_stickyness.store(val, std::memory_order_relaxed);
}
void BrainSession::setMfccWeight(const double val) noexcept {
    impl_->a_mfcc_weight.store(val, std::memory_order_relaxed);
}
void BrainSession::setMelWeight(const double val) noexcept {
    impl_->a_mel_weight.store(val, std::memory_order_relaxed);
}
void BrainSession::setSpectralWeight(const double val) noexcept {
    impl_->a_spectral_weight.store(val, std::memory_order_relaxed);
}
void BrainSession::setNRatio(const double val) noexcept {
    impl_->a_n_ratio.store(val, std::memory_order_relaxed);
}

// ── Effects ───────────────────────────────────────────────────────────────────

void BrainSession::addEffect(const EffectType type) {
    if (type == EffectType::SpectralMorph && !impl_->spectral_morph) {
        auto fft = std::make_shared<adapter::fft::PocketfftBackend>();
        impl_->spectral_morph = std::make_unique<adapter::effects::SpectralMorph>(std::move(fft));
    }
}

void BrainSession::removeEffect(const EffectType type) {
    if (type == EffectType::SpectralMorph) {
        impl_->spectral_morph.reset();
        impl_->morph_feedback.clear();  // discard accumulated spectral state
    }
}

void BrainSession::setEffectAmount(const EffectType type, const double amount) noexcept {
    if (type == EffectType::SpectralMorph) {
        impl_->a_spectral_morph_amount.store(amount, std::memory_order_relaxed);
    }
}

// ── Clear ────────────────────────────────────────────────────────────────────

void BrainSession::clear() noexcept {
    impl_->clear();
}

// ── Ingestion ─────────────────────────────────────────────────────────────────

void BrainSession::addSamples(const double* samples, const std::size_t count, const int sample_rate,
                              const char* name) {
    if (samples == nullptr || count == 0 || sample_rate <= 0) {
        return;
    }
    if (!impl_->brain) {
        impl_->resetBrain();
    }
    const std::string label = (name != nullptr) ? name : "";
    const std::size_t first_block = impl_->brain->size();

    Channel vec(count);
    for (std::size_t k = 0; k < count; ++k) {
        vec[k] = static_cast<float>(samples[k]);
    }
    Sound sound({std::move(vec)}, sample_rate);
    impl_->brain->addSound(sound, label);

    // Record per-block metadata for Swift video mapping.
    // Use the same step that Brain::addSound uses (respects overlap_ratio).
    const std::size_t step = impl_->stepSamples();
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

    std::vector<Channel> ch_data(static_cast<std::size_t>(channels), Channel(frame_count));
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        for (int ch = 0; ch < channels; ++ch) {
            ch_data[static_cast<std::size_t>(ch)][frame] =
                static_cast<float>(samples[(frame * static_cast<std::size_t>(channels)) + ch]);
        }
    }
    Sound sound(std::move(ch_data), sample_rate);
    impl_->brain->addSound(sound, label);

    const std::size_t step = impl_->stepSamples();
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
    if (!impl_->brain || samples == nullptr || count == 0) {
        return 0;
    }
    if (!impl_->play_head.has_value()) {
        impl_->buildPlayHead();
    }
    if (!impl_->play_head.has_value()) {
        return 0;
    }
    // Reuse scratch buffer to avoid heap allocation on every audio-thread call.
    impl_->advance_scratch.assign(samples, samples + count);
    const AudioPrint print = impl_->brain->analyser().analyse(impl_->advance_scratch, sample_rate);
    const BlockAnalysis target{.print = print, .normalised_print = print};
    const SearchParams params = impl_->snapshotParams();
    const std::size_t matched = impl_->play_head->advance(target, params);
    impl_->prev_matched_idx = impl_->last_matched_idx;
    impl_->last_matched_idx = matched;
    if (impl_->ola_buffer && impl_->brain) {
        const auto& blocks = impl_->brain->blocks();
        if (matched < blocks.size()) {
            impl_->ola_buffer->accumulate(blocks[matched].channel_samples);
        }
    }
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
    if (impl_->drift_print.print.mfcc.empty()) {
        impl_->initDriftFromNoise();
    }

    // In infinite mode always penalise the selected block so it can't win again
    // immediately — this is the core mechanism that forces the path forward.
    const SearchParams base_params = impl_->snapshotParams();
    SearchParams infinite_params = base_params;
    infinite_params.usage_weight = std::max(base_params.usage_weight, 0.002);
    infinite_params.usage_falloff = std::min(base_params.usage_falloff, 0.92);

    // Search: find the block most similar to the current target.
    // Guard immediately before dereference — clang-tidy can't track has_value
    // across opaque function calls like initDriftFromNoise().
    if (!impl_->play_head.has_value()) {
        return 0;
    }
    auto& head = *impl_->play_head;
    const std::size_t matched = head.advance(impl_->drift_print, infinite_params);

    // The matched block becomes the next target — this traces a path through
    // timbral space where each block leads toward its most similar neighbour.
    // A tiny noise perturbation prevents the path from being fully deterministic.
    // If the matched block is silent (near-zero energy), jump to a fresh random
    // starting point rather than letting the path get trapped in silence.
    const AudioPrint& mp = impl_->brain->blocks()[matched].analysis.print;
    const double energy = [&] {
        double sum = 0.0;
        for (const float mel_val : mp.mel) {
            sum += static_cast<double>(mel_val) * static_cast<double>(mel_val);
        }
        return sum;
    }();

    if (energy < 1e-6) {
        impl_->initDriftFromNoise();
    } else {
        auto copyWithNoise = [](std::vector<float>& dst, const std::vector<float>& src) {
            dst.resize(src.size());
            for (std::size_t i = 0; i < src.size(); ++i) {
                dst[i] = src[i] + static_cast<float>(rng::randomDouble(-0.05, 0.05));
            }
        };
        copyWithNoise(impl_->drift_print.print.mfcc, mp.mfcc);
        copyWithNoise(impl_->drift_print.print.mel, mp.mel);
        copyWithNoise(impl_->drift_print.print.spectral, mp.spectral);
        impl_->drift_print.normalised_print = impl_->drift_print.print;
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

    impl_->prev_matched_idx = impl_->last_matched_idx;
    impl_->last_matched_idx = matched;
    if (impl_->ola_buffer && impl_->brain) {
        const auto& blocks = impl_->brain->blocks();
        if (matched < blocks.size()) {
            impl_->ola_buffer->accumulate(blocks[matched].channel_samples);
        }
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

std::size_t BrainSession::stepSize() const noexcept {
    if (impl_->ola_buffer && impl_->ola_buffer->active()) {
        return impl_->ola_buffer->stepSize();
    }
    return blockSize();
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
                                          const std::size_t max_count) const {
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
    const std::size_t num_frames = std::min(src.size(), max_count);

    // Apply spectral morph if active — uses feedback cache as `prev` so the
    // morph acts as a spectral IIR filter (reverb-like smearing at high amounts).
    if (impl_->spectral_morph && index == impl_->last_matched_idx.value_or(index + 1)) {
        // Widen the float block to double for the SpectralMorph API.
        auto& scratch = impl_->float_to_double_scratch;
        scratch.assign(src.begin(), src.begin() + num_frames);
        const std::vector<double>& prev_ch =
            (!impl_->morph_feedback.empty() && !impl_->morph_feedback[0].empty())
                ? impl_->morph_feedback[0]
                : scratch;  // first block: prev == curr (identity morph)
        const std::size_t len = std::min({scratch.size(), prev_ch.size(), max_count});
        const auto morphed = impl_->spectral_morph->apply(
            prev_ch, scratch, impl_->a_spectral_morph_amount.load(std::memory_order_relaxed));
        const std::size_t copy_n = std::min(morphed.size(), len);
        // Update feedback cache for next block.
        if (impl_->morph_feedback.empty()) {
            impl_->morph_feedback.resize(1);
        }
        impl_->morph_feedback[0] = morphed;
        std::copy_n(morphed.begin(), static_cast<std::ptrdiff_t>(copy_n), out_buffer);
        return copy_n;
    }

    // Widen float→double: copy_n from float* to double* performs implicit widening.
    std::copy_n(src.begin(), static_cast<std::ptrdiff_t>(num_frames), out_buffer);
    return num_frames;
}

std::size_t BrainSession::getBlockSamplesInterleaved(const std::size_t index, double* out_buffer,
                                                     const std::size_t max_frames) const {
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
    const std::size_t nch = channels.size();

    // ── OLA path ─────────────────────────────────────────────────────────────
    // When OLA is active, output comes from the accumulation buffer (windowed
    // overlap-add), not directly from the matched block's raw samples.
    if (impl_->ola_buffer && impl_->ola_buffer->active() &&
        index == impl_->last_matched_idx.value_or(index + 1)) {
        const std::size_t step = impl_->ola_buffer->stepSize();
        const std::size_t frames = std::min(step, max_frames);

        impl_->ola_buffer->read(impl_->ola_read_scratch);

        // Apply spectral morph to the OLA output (last in the effect chain).
        if (impl_->spectral_morph) {
            if (impl_->morph_feedback.size() < nch) {
                impl_->morph_feedback.resize(nch);
            }
            for (std::size_t chi = 0; chi < nch; ++chi) {
                const auto& curr = impl_->ola_read_scratch[chi];
                const auto& prev =
                    !impl_->morph_feedback[chi].empty() ? impl_->morph_feedback[chi] : curr;
                const auto morphed = impl_->spectral_morph->apply(
                    prev, curr, impl_->a_spectral_morph_amount.load(std::memory_order_relaxed));
                impl_->morph_feedback[chi] = morphed;
                const std::size_t num_frames = std::min(morphed.size(), frames);
                for (std::size_t i = 0; i < num_frames; ++i) {
                    out_buffer[(i * nch) + chi] = morphed[i];
                }
            }
        } else {
            for (std::size_t i = 0; i < frames; ++i) {
                for (std::size_t chi = 0; chi < nch; ++chi) {
                    out_buffer[(i * nch) + chi] = impl_->ola_read_scratch[chi][i];
                }
            }
        }
        return frames;
    }

    // ── Non-OLA path ─────────────────────────────────────────────────────────
    const std::size_t frames = std::min(channels[0].size(), max_frames);

    if (impl_->spectral_morph && index == impl_->last_matched_idx.value_or(index + 1)) {
        if (impl_->morph_feedback.size() < nch) {
            impl_->morph_feedback.resize(nch);
        }
        auto& scratch = impl_->float_to_double_scratch;
        for (std::size_t chi = 0; chi < nch; ++chi) {
            // Widen the float channel to double for SpectralMorph.
            const auto& curr_ch_f = channels[chi];
            scratch.assign(curr_ch_f.begin(), curr_ch_f.end());
            const std::vector<double>& prev_ch =
                !impl_->morph_feedback[chi].empty() ? impl_->morph_feedback[chi] : scratch;
            const auto morphed = impl_->spectral_morph->apply(
                prev_ch, scratch, impl_->a_spectral_morph_amount.load(std::memory_order_relaxed));
            impl_->morph_feedback[chi] = morphed;
            const std::size_t num_frames = std::min(morphed.size(), frames);
            for (std::size_t frame = 0; frame < num_frames; ++frame) {
                out_buffer[(frame * nch) + chi] = morphed[frame];
            }
        }
        return frames;
    }

    // float→double: implicit widening in assignment.
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t chi = 0; chi < nch; ++chi) {
            out_buffer[(frame * nch) + chi] = channels[chi][frame];
        }
    }
    return frames;
}

// ── Block source metadata ─────────────────────────────────────────────────────

std::string BrainSession::getBlockSourceName(const std::size_t index) const {
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

    const auto& first_fp = blocks[0].analysis.print.mfcc;
    std::size_t best = 0;
    double best_dist = std::numeric_limits<double>::max();
    for (std::size_t i = 1; i < blocks.size(); ++i) {
        const double dist =
            impl_->brain->analyser().distance(first_fp, blocks[i].analysis.print.mfcc);
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
       << "  overlap ratio : " << impl_->overlap_ratio << " (step=" << stepSize() << " samples)\n"
       << "  window        : " << win_name << " (synthesis/OLA)\n"
       << "  num_synapses  : " << impl_->num_synapses << "\n"
       << "  strategy      : " << strat_name << "\n"
       << "  index built   : " << (impl_->brain->hasIndex() ? "yes" : "no") << "\n"
       << "  nearest to [0]: block " << best << " (dist=" << best_dist << ")\n";
    return ss.str();
}

}  // namespace audio
// NOLINTEND(readability-make-member-function-const)
