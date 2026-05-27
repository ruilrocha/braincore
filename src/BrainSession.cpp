#include "BrainSession.h"

#include "adapter/analysis/MfccAnalyser.h"
#include "adapter/fft/PocketfftBackend.h"
#include "adapter/search/ClosestSearch.h"
#include "domain/AudioPrint.h"
#include "domain/Brain.h"
#include "domain/PlayHead.h"
#include "domain/SearchParams.h"
#include "domain/Sound.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace audio {

// ── Pimpl struct ──────────────────────────────────────────────────────────────

struct BrainSession::Impl {
    std::shared_ptr<Brain> brain;
    std::optional<PlayHead> play_head;

    Impl() {
        auto fft = std::make_shared<adapter::fft::PocketfftBackend>();
        auto analyser = std::make_shared<adapter::analysis::MfccAnalyser>(std::move(fft));
        brain = std::make_shared<Brain>(std::move(analyser), BlockConfig{});
    }

    void ensurePlayHead() {
        auto strategy = std::make_shared<adapter::search::ClosestSearch>();
        auto const_brain = std::const_pointer_cast<const Brain>(brain);
        play_head.emplace(std::move(const_brain), std::move(strategy));
    }
};

// ── Construction / destruction ────────────────────────────────────────────────

BrainSession::BrainSession() : impl_(std::make_unique<Impl>()) {}
BrainSession::~BrainSession() = default;
BrainSession::BrainSession(BrainSession&&) noexcept = default;
BrainSession& BrainSession::operator=(BrainSession&&) noexcept = default;

// ── Ingestion ─────────────────────────────────────────────────────────────────

void BrainSession::addSamples(const double* samples, const std::size_t count,
                               const int sample_rate, const char* name) {
    std::vector<double> vec(samples, samples + count);
    Sound sound({std::move(vec)}, sample_rate);
    impl_->brain->addSound(sound, name ? name : "");
}

void BrainSession::buildIndex() {
    impl_->brain->buildIndex();
    impl_->ensurePlayHead();
}

// ── Playback ──────────────────────────────────────────────────────────────────

std::size_t BrainSession::advance(const double* samples, const std::size_t count,
                                   const int sample_rate) {
    if (!impl_->play_head) {
        impl_->ensurePlayHead();
    }
    const std::vector<double> vec(samples, samples + count);
    const AudioPrint print = impl_->brain->analyser().analyse(vec, sample_rate);
    const TargetAnalysis target{.print = print, .normalised_print = print};
    return impl_->play_head->advance(target, SearchParams{});
}

// ── Diagnostics ───────────────────────────────────────────────────────────────

std::size_t BrainSession::blockCount() const noexcept {
    return impl_->brain->size();
}

std::size_t BrainSession::blockSize() const noexcept {
    const auto& blocks = impl_->brain->blocks();
    if (blocks.empty()) return 0;
    const auto& ch = blocks[0].channel_samples;
    return ch.empty() ? 0 : ch[0].size();
}

std::size_t BrainSession::getBlockSamples(const std::size_t index, double* out_buffer,
                                           const std::size_t max_count) const noexcept {
    if (out_buffer == nullptr) return 0;
    const auto& blocks = impl_->brain->blocks();
    if (index >= blocks.size()) return 0;
    const auto& ch = blocks[index].channel_samples;
    if (ch.empty()) return 0;
    const auto& src = ch[0];
    const std::size_t n = std::min(src.size(), max_count);
    std::copy(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(n), out_buffer);
    return n;
}

std::string BrainSession::selfTest() {
    const auto& blocks = impl_->brain->blocks();
    if (blocks.empty()) {
        return "BrainSession::selfTest: brain is empty — add sounds first.";
    }

    const auto& first_fp = blocks[0].print.mfcc;

    std::size_t best = 0;
    double best_dist = std::numeric_limits<double>::max();
    for (std::size_t i = 1; i < blocks.size(); ++i) {
        const double d = impl_->brain->analyser().distance(first_fp, blocks[i].print.mfcc);
        if (d < best_dist) {
            best_dist = d;
            best = i;
        }
    }

    std::ostringstream ss;
    ss << "BrainSession selfTest:\n"
       << "  blocks loaded : " << blocks.size() << "\n"
       << "  sources       : " << impl_->brain->sources().size() << "\n"
       << "  MFCC dims     : " << first_fp.size() << "\n"
       << "  nearest to [0]: block " << best << "  (dist=" << best_dist << ")\n"
       << "  index built   : " << (impl_->brain->index() != nullptr ? "yes" : "no") << "\n";

    return ss.str();
}

}  // namespace audio
