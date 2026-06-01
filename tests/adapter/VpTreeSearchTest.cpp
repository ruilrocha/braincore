#include "adapter/search/VpTreeSearch.h"
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
        const auto mfcc_d = compute(b, sr);
        p.mfcc = std::vector<float>(mfcc_d.begin(), mfcc_d.end());
        p.spectral = p.mfcc;
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

// Block size large enough for the Hann window to produce non-zero interior coefficients.
// block_size=1 or 2 yields w[i]=0 everywhere (Hann edges are zero), making all blocks
// indistinguishable after windowing. Use 16 so the interior samples carry signal.
constexpr int kBlockSize = 16;

// Brain with N blocks. Block i has all samples set to the constant value i, producing
// a fingerprint proportional to i*hann_coefficients (distinct for each i > 0 — block 0
// has the all-zero fingerprint, others scale up from there).
std::shared_ptr<audio::Brain> makeBrain(int n_blocks, bool build_index) {
    const audio::BlockConfig cfg{.block_size = kBlockSize};
    auto brain = std::make_shared<audio::Brain>(std::make_shared<IdentityAnalyser>(), cfg);
    for (int i = 0; i < n_blocks; ++i) {
        // Fill every sample with the value i so the fingerprint is i × hann_vector.
        const audio::Channel ch(kBlockSize, static_cast<float>(i));
        brain->addSound(audio::Sound({ch}, 44100));
    }
    if (build_index) {
        brain->buildIndex(static_cast<std::size_t>(n_blocks - 1));
    }
    return brain;
}

}  // namespace

TEST(VpTreeSearch, ThrowsWhenNoIndex) {
    auto brain = makeBrain(5, /*build_index=*/false);
    audio::adapter::search::VpTreeSearch search;
    const audio::SearchParams params;
    std::vector<double> usages(brain->size(), 0.0);
    // Use block 2's own fingerprint as target.
    const auto& target = brain->blocks()[2].analysis;
    audio::SearchContext ctx{*brain, target, params, 0, usages};
    EXPECT_THROW((void)search.search(ctx), std::runtime_error);
}

TEST(VpTreeSearch, ReturnsValidIndexWithIndex) {
    auto brain = makeBrain(5, /*build_index=*/true);
    audio::adapter::search::VpTreeSearch search;
    const audio::SearchParams params;
    std::vector<double> usages(brain->size(), 0.0);
    const auto& target = brain->blocks()[2].analysis;
    audio::SearchContext ctx{*brain, target, params, 0, usages};
    const std::size_t idx = search.search(ctx);
    EXPECT_LT(idx, brain->size());
}

TEST(VpTreeSearch, ReturnsExactMatchBlock) {
    // When the target fingerprint is taken directly from block k, distance to block k
    // is 0, so VpTree must return block k (no other block can score lower).
    auto brain = makeBrain(8, /*build_index=*/true);
    audio::adapter::search::VpTreeSearch search(8);
    const audio::SearchParams params;

    for (std::size_t k : {1u, 3u, 5u, 7u}) {
        std::vector<double> usages(brain->size(), 0.0);
        const auto& target = brain->blocks()[k].analysis;
        audio::SearchContext ctx{*brain, target, params, 0, usages};
        EXPECT_EQ(search.search(ctx), k)
            << "VpTree should return the block whose fingerprint exactly matches the target";
    }
}

TEST(VpTreeSearch, MultipleCallsAllReturnValidIndices) {
    auto brain = makeBrain(8, /*build_index=*/true);
    audio::adapter::search::VpTreeSearch search(8);
    const audio::SearchParams params;
    std::vector<double> usages(brain->size(), 0.0);

    for (std::size_t k = 0; k < brain->size(); ++k) {
        const auto& target = brain->blocks()[k].analysis;
        audio::SearchContext ctx{*brain, target, params, 0, usages};
        EXPECT_LT(search.search(ctx), brain->size());
    }
}

TEST(VpTreeSearch, ResultsAreRepeatableForSameInput) {
    // Given the same context, VpTreeSearch must be deterministic.
    auto brain = makeBrain(6, /*build_index=*/true);
    audio::adapter::search::VpTreeSearch search(6);
    const audio::SearchParams params;
    const auto& target = brain->blocks()[3].analysis;

    std::vector<double> usages1(brain->size(), 0.0);
    audio::SearchContext ctx1{*brain, target, params, 0, usages1};
    const std::size_t first = search.search(ctx1);

    std::vector<double> usages2(brain->size(), 0.0);
    audio::SearchContext ctx2{*brain, target, params, 0, usages2};
    const std::size_t second = search.search(ctx2);

    EXPECT_EQ(first, second);
}
