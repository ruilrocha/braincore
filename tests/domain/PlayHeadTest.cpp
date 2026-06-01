#include "domain/Brain.h"
#include "domain/PlayHead.h"
#include "domain/SearchContext.h"
#include "domain/SearchParams.h"
#include "domain/Sound.h"
#include "domain/port/IAnalyser.h"
#include "domain/port/ISearchStrategy.h"
#include "gtest/gtest.h"

#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

namespace {

// Stub analyser: constant fingerprint.
class StubAnalyser final : public audio::port::IAnalyser {
public:
    [[nodiscard]] std::vector<double> compute(const std::vector<double>& /*b*/,
                                              int /*sr*/) const override {
        return {0.0};
    }
    [[nodiscard]] audio::AudioPrint analyse(const std::vector<double>& /*b*/,
                                            int /*sr*/) const override {
        audio::AudioPrint p;
        p.mfcc = {0.0f};
        p.spectral = {0.0f};
        return p;
    }
    [[nodiscard]] double distance(const std::vector<float>& a,
                                  const std::vector<float>& b) const override {
        double s = 0.0;
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
            const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
            s += d * d;
        }
        return std::sqrt(s);
    }
};

// Stub strategy: always returns (current_index + 1) % brain.size().
class IncrementSearch final : public audio::port::ISearchStrategy {
public:
    [[nodiscard]] std::size_t search(const audio::SearchContext& ctx) const override {
        const std::size_t n = ctx.brain.size();
        return n > 0 ? (ctx.current_block_index + 1) % n : 0;
    }
};

// Helper: monotone Brain with N blocks, no real audio.
std::shared_ptr<const audio::Brain> makeBrain(int n_blocks = 4) {
    constexpr audio::BlockConfig cfg{.block_size = 4};
    auto brain = std::make_shared<audio::Brain>(std::make_shared<StubAnalyser>(), cfg);
    brain->addSound(audio::Sound({audio::Channel(4 * n_blocks, 0.0f)}, 44100));
    return brain;
}

}  // namespace

// ─── Construction ──────────────────────────────────────────────────────────────

TEST(PlayHead, InitialIndexIsZero) {
    auto brain = makeBrain();
    audio::PlayHead ph(brain, std::make_shared<IncrementSearch>());
    EXPECT_EQ(ph.currentIndex(), 0u);
}

TEST(PlayHead, BlockUsagesSizedToBrain) {
    auto brain = makeBrain(6);
    audio::PlayHead ph(brain, std::make_shared<IncrementSearch>());
    EXPECT_EQ(ph.blockUsages().size(), brain->size());
}

TEST(PlayHead, BlockUsagesInitialisedToZero) {
    auto brain = makeBrain(4);
    audio::PlayHead ph(brain, std::make_shared<IncrementSearch>());
    for (const double u : ph.blockUsages()) {
        EXPECT_EQ(u, 0.0);
    }
}

// ─── advance() ────────────────────────────────────────────────────────────────

TEST(PlayHead, AdvanceReturnsValidIndex) {
    auto brain = makeBrain(4);
    audio::PlayHead ph(brain, std::make_shared<IncrementSearch>());
    const audio::BlockAnalysis target;
    const audio::SearchParams params;
    const std::size_t idx = ph.advance(target, params);
    EXPECT_LT(idx, brain->size());
}

TEST(PlayHead, AdvanceUpdatesCurrentIndex) {
    auto brain = makeBrain(4);
    audio::PlayHead ph(brain, std::make_shared<IncrementSearch>());
    const audio::BlockAnalysis target;
    const audio::SearchParams params;
    const std::size_t idx = ph.advance(target, params);
    EXPECT_EQ(ph.currentIndex(), idx);
}

TEST(PlayHead, AdvanceMultipleTimesStaysInBounds) {
    auto brain = makeBrain(4);
    audio::PlayHead ph(brain, std::make_shared<IncrementSearch>());
    const audio::BlockAnalysis target;
    const audio::SearchParams params;
    for (int i = 0; i < 20; ++i) {
        const std::size_t idx = ph.advance(target, params);
        EXPECT_LT(idx, brain->size());
    }
}

// ─── reset() ──────────────────────────────────────────────────────────────────

TEST(PlayHead, ResetRestoresIndexToZero) {
    auto brain = makeBrain(4);
    audio::PlayHead ph(brain, std::make_shared<IncrementSearch>());
    const audio::BlockAnalysis target;
    const audio::SearchParams params;
    ph.advance(target, params);
    ph.advance(target, params);
    ph.reset();
    EXPECT_EQ(ph.currentIndex(), 0u);
}

TEST(PlayHead, ResetZerosUsageCounters) {
    auto brain = makeBrain(4);
    audio::PlayHead ph(brain, std::make_shared<IncrementSearch>());
    const audio::BlockAnalysis target;
    const audio::SearchParams params;
    ph.advance(target, params);
    ph.reset();
    for (const double u : ph.blockUsages()) {
        EXPECT_EQ(u, 0.0);
    }
}

// ─── rebind() ─────────────────────────────────────────────────────────────────

TEST(PlayHead, RebindResizesUsagesToNewBrain) {
    auto brain4 = makeBrain(4);
    auto brain7 = makeBrain(7);
    audio::PlayHead ph(brain4, std::make_shared<IncrementSearch>());
    EXPECT_EQ(ph.blockUsages().size(), 4u);

    ph.rebind(brain7, std::make_shared<IncrementSearch>());
    EXPECT_EQ(ph.blockUsages().size(), 7u);
}

TEST(PlayHead, RebindResetsIndexToZero) {
    auto brain = makeBrain(4);
    audio::PlayHead ph(brain, std::make_shared<IncrementSearch>());
    const audio::BlockAnalysis target;
    const audio::SearchParams params;
    ph.advance(target, params);

    ph.rebind(makeBrain(4), std::make_shared<IncrementSearch>());
    EXPECT_EQ(ph.currentIndex(), 0u);
}

// ─── depleteUsages() ─────────────────────────────────────────────────────────

TEST(PlayHead, DepleteUsagesScalesAllCounters) {
    auto brain = makeBrain(4);
    audio::PlayHead ph(brain, std::make_shared<IncrementSearch>());

    // Artificially set some usage counters via advance (strategy increments usage).
    const audio::BlockAnalysis target;
    audio::SearchParams params;
    params.usage_weight = 1.0;
    params.usage_falloff = 1.0;
    ph.advance(target, params);
    ph.advance(target, params);

    const auto before = ph.blockUsages();
    ph.depleteUsages(0.5);
    const auto after = ph.blockUsages();

    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_NEAR(after[i], before[i] * 0.5, 1e-12);
    }
}
