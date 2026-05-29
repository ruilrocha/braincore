#include "adapter/search/MomentumSearch.h"
#include "domain/Block.h"
#include "domain/Brain.h"
#include "domain/SearchContext.h"
#include "domain/SearchParams.h"
#include "domain/Sound.h"
#include "domain/port/IAnalyser.h"
#include "gtest/gtest.h"

#include <cmath>
#include <memory>
#include <vector>

namespace {

class IdentityAnalyser final : public audio::port::IAnalyser {
public:
    [[nodiscard]] std::vector<double> compute(const std::vector<double>& b,
                                              int /*sr*/) const override {
        return b;
    }
    [[nodiscard]] audio::AudioPrint analyse(const std::vector<double>& b,
                                            const int sr) const override {
        audio::AudioPrint p;
        p.mfcc = compute(b, sr);
        p.spectral = p.mfcc;
        return p;
    }
    [[nodiscard]] double distance(const std::vector<double>& a,
                                  const std::vector<double>& b) const override {
        double s = 0.0;
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
            const double d = a[i] - b[i];
            s += d * d;
        }
        return std::sqrt(s);
    }
};

// Block size large enough for the Hann window to produce non-zero interior coefficients.
// block_size=1 yields w[0]=0, making all fingerprints zero and indistinguishable.
constexpr int kBlockSize = 16;

// Brain with N blocks. Block i has all samples equal to i, giving fingerprint i × hann.
std::shared_ptr<audio::Brain> makeBrain(int n_blocks) {
    const audio::BlockConfig cfg{.block_size = kBlockSize};
    auto brain = std::make_shared<audio::Brain>(std::make_shared<IdentityAnalyser>(), cfg);
    for (int i = 0; i < n_blocks; ++i) {
        const audio::Channel ch(kBlockSize, static_cast<double>(i));
        brain->addSound(audio::Sound({ch}, 44100));
    }
    return brain;
}

}  // namespace

// ─── MomentumSearch ───────────────────────────────────────────────────────────

TEST(MomentumSearch, ReturnsValidIndex) {
    auto brain = makeBrain(5);
    audio::adapter::search::MomentumSearch search;
    audio::SearchParams params;
    params.stickyness = 0.0;
    params.usage_weight = 0.0;
    std::vector<double> usages(brain->size(), 0.0);
    const auto& target = brain->blocks()[2].analysis;
    audio::SearchContext ctx{*brain, target, params, 0, usages};
    EXPECT_LT(search.search(ctx), brain->size());
}

TEST(MomentumSearch, ZeroMomentumBehavesLikeClosestSearch) {
    // With momentum=0 the velocity term is multiplied by 0, so the blended target
    // equals the actual target fingerprint exactly. The result must be the block whose
    // fingerprint is identical to the target (distance = 0).
    auto brain = makeBrain(8);
    audio::adapter::search::MomentumSearch search;
    audio::SearchParams params;
    params.momentum = 0.0;
    params.stickyness = 0.0;
    params.usage_weight = 0.0;

    for (std::size_t k : {1u, 3u, 5u, 7u}) {
        std::vector<double> usages(brain->size(), 0.0);
        // Use block k's own fingerprint → distance to block k is 0 → must win.
        const auto& target = brain->blocks()[k].analysis;
        audio::SearchContext ctx{*brain, target, params, 0, usages};
        EXPECT_EQ(search.search(ctx), k)
            << "momentum=0 should pick the block whose fingerprint equals the target";
    }
}

TEST(MomentumSearch, HighMomentumIgnoresTargetOnFirstCall) {
    // On the very first call the internal velocity is 0, so:
    //   blended_mfcc = target * (1-1) + (current + 0) * 1 = current_block.mfcc
    // → search scores all blocks against the CURRENT block's fingerprint, not the target.
    // When the target is far from the current block, the two momentum values must return
    // different results, demonstrating that momentum=1 is governed by inertia, not target.
    auto brain = makeBrain(10);
    audio::SearchParams params;
    params.stickyness = 0.0;
    params.usage_weight = 0.0;

    // Current = block 0, target = block 9 (far away).
    const auto& target = brain->blocks()[9].analysis;
    std::vector<double> usages(brain->size(), 0.0);

    {
        // Zero momentum: picks closest to target (block 9 itself, distance = 0).
        audio::adapter::search::MomentumSearch s;
        params.momentum = 0.0;
        audio::SearchContext ctx{*brain, target, params, 0, usages};
        EXPECT_EQ(s.search(ctx), 9u) << "momentum=0 must pick the target block (distance 0)";
    }
    {
        // Full momentum: blended = current = block 0 → picks block 0 (distance 0).
        audio::adapter::search::MomentumSearch s;
        params.momentum = 1.0;
        audio::SearchContext ctx{*brain, target, params, 0, usages};
        EXPECT_EQ(s.search(ctx), 0u)
            << "momentum=1 first-call must follow current, not jump to target";
    }
}

TEST(MomentumSearch, HighMomentumOvershoots) {
    // After a step that moves from block 0 to block 5, the velocity points in the
    // direction 0→5. On the next call with the same target (block 5), high momentum
    // blends in the velocity → the predicted position overshoots past 5. The result
    // should therefore be > 5.
    auto brain = makeBrain(10);
    audio::SearchParams params;
    params.stickyness = 0.0;
    params.usage_weight = 0.0;
    params.momentum = 1.0;
    params.momentum_decay = 0.5;

    // Target: block 5's fingerprint.
    const auto& target = brain->blocks()[5].analysis;
    std::vector<double> usages(brain->size(), 0.0);

    audio::adapter::search::MomentumSearch search;

    // Call 1: current=0, no velocity yet → returns 0 (full momentum, no target pull).
    audio::SearchContext ctx1{*brain, target, params, 0, usages};
    const std::size_t r1 = search.search(ctx1);
    EXPECT_LT(r1, brain->size());

    // Call 2: current=5 (simulate jumping to block 5).
    // prev_fp_ = blocks[0].mfcc; current_mfcc = blocks[5].mfcc
    // delta = blocks[5] - blocks[0] = big positive; velocity gets a rightward push.
    // predicted = blocks[5] + velocity → overshoots past 5 → result > 5.
    audio::SearchContext ctx2{*brain, target, params, 5, usages};
    const std::size_t r2 = search.search(ctx2);
    EXPECT_GT(r2, 5u) << "After moving 0→5, high momentum should overshoot the target";
    EXPECT_LT(r2, brain->size());
}

TEST(MomentumSearch, MultipleCallsRemainInBounds) {
    auto brain = makeBrain(8);
    audio::adapter::search::MomentumSearch search;
    audio::SearchParams params;
    params.momentum = 0.8;
    params.momentum_decay = 0.9;
    params.stickyness = 0.0;
    params.usage_weight = 0.0;

    std::vector<double> usages(brain->size(), 0.0);
    std::size_t cur = 0;
    for (std::size_t i = 0; i < brain->size(); ++i) {
        const auto& target = brain->blocks()[i].analysis;
        audio::SearchContext ctx{*brain, target, params, cur, usages};
        cur = search.search(ctx);
        EXPECT_LT(cur, brain->size());
    }
}

TEST(MomentumSearch, ResultsRemainInBoundsWithHighMomentum) {
    auto brain = makeBrain(10);
    audio::adapter::search::MomentumSearch search;
    audio::SearchParams params;
    params.momentum = 1.0;
    params.momentum_decay = 0.5;
    params.stickyness = 0.0;
    params.usage_weight = 0.0;

    std::vector<double> usages(brain->size(), 0.0);
    std::size_t cur = 0;
    for (std::size_t i = 0; i < brain->size() * 3; ++i) {
        const auto& target = brain->blocks()[i % brain->size()].analysis;
        audio::SearchContext ctx{*brain, target, params, cur, usages};
        cur = search.search(ctx);
        EXPECT_LT(cur, brain->size()) << "Out of bounds at step " << i;
    }
}
