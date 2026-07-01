#include "adapter/search/ClosestSearch.h"
#include "domain/Block.h"
#include "domain/Brain.h"
#include "domain/SearchContext.h"
#include "domain/SearchParams.h"
#include "domain/port/IAnalyser.h"
#include "gtest/gtest.h"

#include <cmath>
#include <memory>
#include <vector>

namespace {

// Euclidean distance analyser: primary fingerprint is the block's raw samples
// passed through as-is. Useful for testing without MFCC overhead.
class EuclideanAnalyser final : public audio::port::IAnalyser {
public:
    [[nodiscard]] std::vector<double> compute(const std::vector<double>& block,
                                              int /*sr*/) const override {
        return block;
    }

    [[nodiscard]] audio::AudioPrint analyse(const std::vector<double>& block,
                                            const int sr) const override {
        audio::AudioPrint p;
        const auto mfcc_d = compute(block, sr);
        p.mfcc = std::vector<float>(mfcc_d.begin(), mfcc_d.end());
        p.mel = p.mfcc;
        p.spectral = p.mfcc;
        return p;
    }

    [[nodiscard]] double distance(const std::vector<float>& a,
                                  const std::vector<float>& b) const override {
        double sum = 0.0;
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
            const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
            sum += d * d;
        }
        return std::sqrt(sum);
    }
};

// Helper: build a block with a given mfcc fingerprint.
audio::Block makeBlock(const std::vector<float>& mfcc) {
    audio::Block b;
    b.analysis.print.mfcc = mfcc;
    b.analysis.print.spectral = mfcc;
    b.analysis.normalised_print.mfcc = mfcc;
    b.analysis.normalised_print.spectral = mfcc;
    return b;
}

// Helper: build a Brain from pre-fingerprinted blocks.
std::shared_ptr<audio::Brain> makeBrain(const std::vector<audio::Block>& blocks) {
    auto analyser = std::make_shared<EuclideanAnalyser>();
    return audio::Brain::rebuild(blocks, std::move(analyser), audio::BlockConfig{});
}

// Helper: run a search and return the matched index.
std::size_t runSearch(audio::adapter::search::ClosestSearch& search, const audio::Brain& brain,
                      const audio::BlockAnalysis& target, const audio::SearchParams& params,
                      std::vector<double>& block_usages, std::size_t current = 0) {
    audio::SearchContext ctx{brain, target, params, current, block_usages};
    return search.search(ctx);
}

}  // namespace

TEST(ClosestSearch, PicksNearestBlock) {
    // Block 0: fingerprint {0.0}, Block 1: {1.0}, Block 2: {2.0}
    // Target: {0.1} → closest is block 0.
    const std::vector blocks = {
        makeBlock({0.0f}),
        makeBlock({1.0f}),
        makeBlock({2.0f}),
    };

    auto brain = makeBrain(blocks);
    audio::adapter::search::ClosestSearch search;
    audio::SearchParams params;
    params.stickyness = 0.0;
    params.usage_weight = 0.0;

    std::vector<double> block_usages(blocks.size(), 0.0);
    audio::BlockAnalysis target;
    target.print.mfcc = {0.1f};
    target.normalised_print.mfcc = {0.1f};
    const std::size_t idx = runSearch(search, *brain, target, params, block_usages);
    EXPECT_EQ(idx, 0U);
}

TEST(ClosestSearch, PicksMiddleBlockWhenClosest) {
    const std::vector blocks = {
        makeBlock({0.0f}),
        makeBlock({1.0f}),
        makeBlock({2.0f}),
    };

    auto brain = makeBrain(blocks);
    audio::adapter::search::ClosestSearch search;
    audio::SearchParams params;
    params.stickyness = 0.0;
    params.usage_weight = 0.0;

    std::vector<double> block_usages(blocks.size(), 0.0);
    audio::BlockAnalysis target;
    target.print.mfcc = {0.9f};
    target.normalised_print.mfcc = {0.9f};
    const std::size_t idx = runSearch(search, *brain, target, params, block_usages);
    EXPECT_EQ(idx, 1U);
}

TEST(ClosestSearch, UsagePenaltyShiftsSelection) {
    // Block 0 is closest, but has high usage. With usage_weight > 0 the
    // penalised score of block 0 should exceed block 1's score, so block 1 wins.
    const std::vector blocks = {
        makeBlock({0.0f}),  // closest but will be penalised
        makeBlock({1.0f}),
    };
    std::vector<double> block_usages(blocks.size(), 0.0);
    block_usages[0] = 1000.0;  // kUsageFactor worth of usage

    auto brain = makeBrain(blocks);
    audio::adapter::search::ClosestSearch search;
    audio::SearchParams params;
    params.stickyness = 0.0;
    params.usage_weight = 1.0;

    const std::vector<float> target_vec = {0.0f};
    audio::BlockAnalysis target;
    target.print.mfcc = target_vec;
    target.normalised_print.mfcc = target_vec;
    const std::size_t idx = runSearch(search, *brain, target, params, block_usages);
    EXPECT_EQ(idx, 1U);
}

TEST(ClosestSearch, SingleBlockAlwaysSelected) {
    const std::vector blocks = {makeBlock({42.0f})};

    auto brain = makeBrain(blocks);
    audio::adapter::search::ClosestSearch search;
    constexpr audio::SearchParams params;

    std::vector<double> block_usages(blocks.size(), 0.0);
    audio::BlockAnalysis target;
    target.print.mfcc = {0.0f};
    target.normalised_print.mfcc = {0.0f};
    const std::size_t idx = runSearch(search, *brain, target, params, block_usages);
    EXPECT_EQ(idx, 0U);
}

TEST(ClosestSearch, StickynessPrefersContinuation) {
    // Block 0: closest to target, Block 1: farther.
    // With stickyness=1.0 and current_idx=0, the next block (1) is always preferred
    // as long as closest_score > 0 (which it is here: target != block 0).
    const std::vector blocks = {
        makeBlock({0.0f}),
        makeBlock({5.0f}),
    };
    auto brain = makeBrain(blocks);
    audio::adapter::search::ClosestSearch search;
    audio::SearchParams params;
    params.stickyness = 1.0;
    params.usage_weight = 0.0;

    std::vector<double> block_usages(blocks.size(), 0.0);
    audio::BlockAnalysis target;
    target.print.mfcc = {0.1f};  // closest to block 0
    target.normalised_print.mfcc = {0.1f};
    // current_idx=0 → next block is 1; with stickyness=1 next always wins
    const std::size_t idx = runSearch(search, *brain, target, params, block_usages, /*current=*/0);
    EXPECT_EQ(idx, 1U);
}

TEST(ClosestSearch, NRatioUsesNormalisedPrint) {
    // Block 0: raw mfcc={0.0}, normalised mfcc={1.0}  (raw matches target raw, norm doesn't)
    // Block 1: raw mfcc={10.0}, normalised mfcc={0.0} (raw doesn't match, but norm matches)
    // Target:  raw mfcc={0.0}, normalised mfcc={0.0}
    //
    // n_ratio=0 → pure raw comparison → block 0 wins (raw distance = 0)
    // n_ratio=1 → pure normalised comparison → block 1 wins (norm distance = 0)
    auto makeCustomBlock = [](std::vector<float> raw, std::vector<float> norm) {
        audio::Block b;
        b.analysis.print.mfcc = raw;
        b.analysis.normalised_print.mfcc = norm;
        b.analysis.print.spectral = raw;
        b.analysis.normalised_print.spectral = norm;
        return b;
    };

    const std::vector blocks = {
        makeCustomBlock({0.0f}, {1.0f}),   // block 0
        makeCustomBlock({10.0f}, {0.0f}),  // block 1
    };
    auto brain = makeBrain(blocks);
    audio::adapter::search::ClosestSearch search;
    audio::SearchParams params;
    params.stickyness = 0.0;
    params.usage_weight = 0.0;

    audio::BlockAnalysis target;
    target.print.mfcc = {0.0f};
    target.normalised_print.mfcc = {0.0f};

    // n_ratio=0: raw comparison, block 0 wins
    params.n_ratio = 0.0;
    std::vector<double> usages(2, 0.0);
    EXPECT_EQ(runSearch(search, *brain, target, params, usages), 0U);

    // n_ratio=1: normalised comparison, block 1 wins
    params.n_ratio = 1.0;
    std::fill(usages.begin(), usages.end(), 0.0);
    EXPECT_EQ(runSearch(search, *brain, target, params, usages), 1U);
}
