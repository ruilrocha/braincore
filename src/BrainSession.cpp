#include "BrainSession.h"

// NOLINTBEGIN(readability-make-member-function-const)
// BrainSession uses the Pimpl idiom: all methods access state through a
// unique_ptr<Impl>, so the pointer itself never changes — the compiler
// considers every method "can be const". Suppressed here only; internal
// domain classes are still checked.

#include "domain/Sound.h"
#include "engine/BrainEngine.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace audio {

// ── Pimpl ─────────────────────────────────────────────────────────────────────

struct BrainSession::Impl {
    BrainEngine engine;
};

// ── Construction ──────────────────────────────────────────────────────────────

BrainSession::BrainSession() : impl_(std::make_unique<Impl>()) {}
BrainSession::~BrainSession() = default;
BrainSession::BrainSession(BrainSession&&) noexcept = default;
BrainSession& BrainSession::operator=(BrainSession&&) noexcept = default;

// ── Brain config ──────────────────────────────────────────────────────────────

void BrainSession::setBlockSize(const int block_size) noexcept {
    impl_->engine.setBlockSize(block_size);
}
int BrainSession::getBlockSize() const noexcept {
    return impl_->engine.getBlockSize();
}

void BrainSession::setWindowShape(const WindowShape shape) noexcept {
    impl_->engine.setWindowShape(shape);
}
WindowShape BrainSession::getWindowShape() const noexcept {
    return impl_->engine.getWindowShape();
}

void BrainSession::setOverlapRatio(const double ratio) noexcept {
    impl_->engine.setOverlapRatio(ratio);
}
double BrainSession::getOverlapRatio() const noexcept {
    return impl_->engine.getOverlapRatio();
}

void BrainSession::setNumSynapses(const std::size_t n) noexcept {
    impl_->engine.setNumSynapses(n);
}
std::size_t BrainSession::getNumSynapses() const noexcept {
    return impl_->engine.getNumSynapses();
}

// ── Search strategy ───────────────────────────────────────────────────────────

void BrainSession::setSearchStrategy(const SearchStrategy strategy) {
    impl_->engine.setSearchStrategy(strategy);
}
SearchStrategy BrainSession::searchStrategy() const noexcept {
    return impl_->engine.searchStrategy();
}

// ── Search params ─────────────────────────────────────────────────────────────

void BrainSession::setUsageWeight(const double val) noexcept {
    impl_->engine.setUsageWeight(val);
}
void BrainSession::setUsageFalloff(const double val) noexcept {
    impl_->engine.setUsageFalloff(val);
}
void BrainSession::setStickyness(const double val) noexcept {
    impl_->engine.setStickyness(val);
}
void BrainSession::setMfccWeight(const double val) noexcept {
    impl_->engine.setMfccWeight(val);
}
void BrainSession::setMelWeight(const double val) noexcept {
    impl_->engine.setMelWeight(val);
}
void BrainSession::setSpectralWeight(const double val) noexcept {
    impl_->engine.setSpectralWeight(val);
}
void BrainSession::setNRatio(const double val) noexcept {
    impl_->engine.setNRatio(val);
}
void BrainSession::setBrightnessTarget(const double val) noexcept {
    impl_->engine.setBrightnessTarget(val);
}
void BrainSession::setBrightnessWeight(const double val) noexcept {
    impl_->engine.setBrightnessWeight(val);
}

// ── Effects ───────────────────────────────────────────────────────────────────

void BrainSession::addEffect(const EffectType type) {
    impl_->engine.addEffect(type);
}
void BrainSession::removeEffect(const EffectType type) {
    impl_->engine.removeEffect(type);
}
void BrainSession::setEffectAmount(const EffectType type, const double amount) noexcept {
    impl_->engine.setEffectAmount(type, amount);
}

// ── Clear ────────────────────────────────────────────────────────────────────

void BrainSession::clear() noexcept {
    impl_->engine.clear();
}

// ── Ingestion ─────────────────────────────────────────────────────────────────

void BrainSession::addSamples(const double* samples, const std::size_t count, const int sample_rate,
                              const char* name) {
    if (samples == nullptr || count == 0 || sample_rate <= 0) {
        return;
    }
    const std::string label = (name != nullptr) ? name : "";
    Channel vec(count);
    for (std::size_t k = 0; k < count; ++k) {
        vec[k] = static_cast<float>(samples[k]);
    }
    const Sound sound({std::move(vec)}, sample_rate);
    impl_->engine.addSound(sound, label);
}

void BrainSession::addSamplesInterleaved(const double* samples, const std::size_t frame_count,
                                         const int channels, const int sample_rate,
                                         const char* name) {
    if (samples == nullptr || channels <= 0 || frame_count == 0 || sample_rate <= 0) {
        return;
    }
    const std::string label = (name != nullptr) ? name : "";
    std::vector ch_data(static_cast<std::size_t>(channels), Channel(frame_count));
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        for (int ch = 0; ch < channels; ++ch) {
            ch_data[static_cast<std::size_t>(ch)][frame] =
                static_cast<float>(samples[(frame * static_cast<std::size_t>(channels)) + ch]);
        }
    }
    Sound sound(std::move(ch_data), sample_rate);
    impl_->engine.addSound(sound, label);
}

void BrainSession::buildIndex() {
    impl_->engine.buildIndex();
}

// ── Playback ──────────────────────────────────────────────────────────────────

std::size_t BrainSession::advance(const double* samples, const std::size_t count,
                                  const int sample_rate) {
    if (samples == nullptr || count == 0) {
        return 0;
    }
    const std::vector buf(samples, samples + count);
    return impl_->engine.advance(buf, sample_rate);
}

std::size_t BrainSession::advanceInfinite(const int sample_rate) {
    return impl_->engine.advanceInfinite(sample_rate);
}

// ── Block data ────────────────────────────────────────────────────────────────

std::size_t BrainSession::blockCount() const noexcept {
    return impl_->engine.blockCount();
}
std::size_t BrainSession::blockSize() const noexcept {
    return impl_->engine.blockSize();
}
std::size_t BrainSession::stepSize() const noexcept {
    return impl_->engine.stepSize();
}
int BrainSession::blockChannels(const std::size_t index) const noexcept {
    return impl_->engine.blockChannels(index);
}

std::size_t BrainSession::getBlockSamples(const std::size_t index, double* out_buffer,
                                          const std::size_t max_count) const {
    return impl_->engine.getBlockSamples(index, out_buffer, max_count);
}

std::size_t BrainSession::getBlockSamplesInterleaved(const std::size_t index, double* out_buffer,
                                                     const std::size_t max_frames) const {
    return impl_->engine.getBlockSamplesInterleaved(index, out_buffer, max_frames);
}

// ── Block source metadata ─────────────────────────────────────────────────────

std::string BrainSession::getBlockSourceName(const std::size_t index) const {
    return impl_->engine.getBlockSourceName(index);
}
double BrainSession::getBlockTimeOffset(const std::size_t index) const noexcept {
    return impl_->engine.getBlockTimeOffset(index);
}

}  // namespace audio
// NOLINTEND(readability-make-member-function-const)
