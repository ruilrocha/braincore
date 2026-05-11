#include "adapter/search/ClosestSearch.h"
#include "domain/Block.h"
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
        p.mfcc = compute(block, sr);
        p.spectral = p.mfcc;
        p.normalised_mfcc = p.mfcc;
        p.normalised_spectral = p.mfcc;
        return p;
    }

    [[nodiscard]] double distance(const std::vector<double>& a,
                                  const std::vector<double>& b) const override {
        double sum = 0.0;
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
            const double d = a[i] - b[i];
            sum += d * d;
        }
        return std::sqrt(sum);
    }
};

// Helper: build a block with a given mfcc fingerprint.
audio::Block makeBlock(const std::vector<double>& mfcc) {
    audio::Block b;
    b.print.mfcc = mfcc;
    b.print.spectral = mfcc;
    b.normalised_print.mfcc = mfcc;
    b.normalised_print.spectral = mfcc;
    b.samples = mfcc;
    return b;
}

}  // namespace

TEST(ClosestSearch, PicksNearestBlock) {
    // Block 0: fingerprint {0.0}, Block 1: {1.0}, Block 2: {2.0}
    // Target: {0.1} → closest is block 0.
    std::vector blocks = {
        makeBlock({0.0}),
        makeBlock({1.0}),
        makeBlock({2.0}),
    };

    EuclideanAnalyser analyser;
    audio::adapter::search::ClosestSearch search;
    audio::SearchParams params;
    params.stickyness = 0.0;
    params.usage_weight = 0.0;

    std::vector<double> block_usages(blocks.size(), 0.0);
    const std::vector target = {0.1};
    const std::size_t idx = search.search(target, blocks, analyser, params, 0, block_usages);
    EXPECT_EQ(idx, 0U);
}

TEST(ClosestSearch, PicksMiddleBlockWhenClosest) {
    std::vector blocks = {
        makeBlock({0.0}),
        makeBlock({1.0}),
        makeBlock({2.0}),
    };

    const EuclideanAnalyser analyser;
    const audio::adapter::search::ClosestSearch search;
    audio::SearchParams params;
    params.stickyness = 0.0;
    params.usage_weight = 0.0;

    std::vector<double> block_usages(blocks.size(), 0.0);
    const std::vector target = {0.9};
    const std::size_t idx = search.search(target, blocks, analyser, params, 0, block_usages);
    EXPECT_EQ(idx, 1U);
}

TEST(ClosestSearch, UsagePenaltyShiftsSelection) {
    // Block 0 is closest, but has high usage. With usage_weight > 0 the
    // penalised score of block 0 should exceed block 1's score, so block 1 wins.
    std::vector blocks = {
        makeBlock({0.0}),  // closest but will be penalised
        makeBlock({1.0}),
    };
    std::vector<double> block_usages(blocks.size(), 0.0);
    block_usages[0] = 1000.0;  // kUsageFactor worth of usage

    EuclideanAnalyser analyser;
    audio::adapter::search::ClosestSearch search;
    audio::SearchParams params;
    params.stickyness = 0.0;
    params.usage_weight = 1.0;

    const std::vector target = {0.0};
    const std::size_t idx = search.search(target, blocks, analyser, params, 0, block_usages);
    EXPECT_EQ(idx, 1U);
}

TEST(ClosestSearch, SingleBlockAlwaysSelected) {
    std::vector blocks = {makeBlock({42.0})};

    const EuclideanAnalyser analyser;
    const audio::adapter::search::ClosestSearch search;
    constexpr audio::SearchParams params;

    std::vector<double> block_usages(blocks.size(), 0.0);
    const std::vector target = {0.0};
    const std::size_t idx = search.search(target, blocks, analyser, params, 0, block_usages);
    EXPECT_EQ(idx, 0U);
}
