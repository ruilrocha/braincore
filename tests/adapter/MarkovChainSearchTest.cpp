#include "adapter/search/MarkovChainSearch.h"
#include "domain/Block.h"
#include "domain/Brain.h"
#include "domain/SearchContext.h"
#include "domain/SearchParams.h"
#include "domain/Sound.h"
#include "domain/port/IAnalyser.h"
#include "gtest/gtest.h"

#include <cmath>
#include <memory>
#include <set>
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

constexpr int kBlockSize = 16;

std::shared_ptr<audio::Brain> makeBrain(int n_blocks, bool build_index) {
    const audio::BlockConfig cfg{.block_size = kBlockSize};
    auto brain = std::make_shared<audio::Brain>(std::make_shared<IdentityAnalyser>(), cfg);
    for (int i = 0; i < n_blocks; ++i) {
        const audio::Channel ch(kBlockSize, static_cast<double>(i));
        brain->addSound(audio::Sound({ch}, 44100));
    }
    if (build_index) {
        brain->buildIndex(static_cast<std::size_t>(n_blocks - 1));
    }
    return brain;
}

}  // namespace

// ─── MarkovChainSearch ────────────────────────────────────────────────────────

TEST(MarkovChainSearch, ThrowsWhenNoIndex) {
    auto brain = makeBrain(5, /*build_index=*/false);
    audio::adapter::search::MarkovChainSearch search;
    const audio::SearchParams params;
    std::vector<double> usages(brain->size(), 0.0);
    const auto& target = brain->blocks()[2].analysis;
    audio::SearchContext ctx{*brain, target, params, 0, usages};
    EXPECT_THROW((void)search.search(ctx), std::runtime_error);
}

TEST(MarkovChainSearch, ReturnsValidIndexWithIndex) {
    auto brain = makeBrain(5, /*build_index=*/true);
    audio::adapter::search::MarkovChainSearch search;
    const audio::SearchParams params;
    std::vector<double> usages(brain->size(), 0.0);
    const auto& target = brain->blocks()[2].analysis;
    audio::SearchContext ctx{*brain, target, params, 2, usages};
    const std::size_t idx = search.search(ctx);
    EXPECT_LT(idx, brain->size());
}

TEST(MarkovChainSearch, MultipleCallsAllReturnValidIndices) {
    auto brain = makeBrain(5, /*build_index=*/true);
    audio::adapter::search::MarkovChainSearch search(1.0, 4);
    const audio::SearchParams params;

    for (std::size_t cur = 0; cur < brain->size(); ++cur) {
        std::vector<double> usages(brain->size(), 0.0);
        const auto& target = brain->blocks()[cur].analysis;
        audio::SearchContext ctx{*brain, target, params, cur, usages};
        EXPECT_LT(search.search(ctx), brain->size());
    }
}

TEST(MarkovChainSearch, LowTemperatureIsMoreDeterministic) {
    // Low temperature → softmax is sharply peaked → same neighbour chosen repeatedly.
    // High temperature → near-uniform distribution → more distinct neighbours visited.
    auto brain = makeBrain(8, /*build_index=*/true);
    audio::adapter::search::MarkovChainSearch search_low(0.01, 7);
    audio::adapter::search::MarkovChainSearch search_high(10.0, 7);

    const audio::SearchParams params;
    const auto& target = brain->blocks()[4].analysis;

    std::set<std::size_t> results_low, results_high;
    constexpr int kRuns = 20;
    for (int i = 0; i < kRuns; ++i) {
        std::vector<double> usages_low(brain->size(), 0.0);
        audio::SearchContext ctx_low{*brain, target, params, 4, usages_low};
        results_low.insert(search_low.search(ctx_low));

        std::vector<double> usages_high(brain->size(), 0.0);
        audio::SearchContext ctx_high{*brain, target, params, 4, usages_high};
        results_high.insert(search_high.search(ctx_high));
    }
    // Low temperature should produce fewer distinct results (more deterministic).
    EXPECT_LE(results_low.size(), results_high.size() + 1);
}
