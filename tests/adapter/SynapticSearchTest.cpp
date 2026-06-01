#include "adapter/search/SynapticSearch.h"
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

// Identity analyser: fingerprint = raw samples passed through.
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

constexpr int kBlockSize = 16;

// Brain with N blocks. Block i has all samples equal to i, giving a fingerprint
// proportional to i × hann_vector — distinct for each i.
std::shared_ptr<audio::Brain> makeBrain(int n_blocks = 5, bool build_index = false) {
    const audio::BlockConfig cfg{.block_size = kBlockSize};
    auto brain = std::make_shared<audio::Brain>(std::make_shared<IdentityAnalyser>(), cfg);
    for (int i = 0; i < n_blocks; ++i) {
        const audio::Channel ch(kBlockSize, static_cast<float>(i));
        brain->addSound(audio::Sound({ch}, 44100));
    }
    if (build_index) {
        brain->buildIndex(n_blocks - 1);
    }
    return brain;
}

}  // namespace

// ─── SynapticSearch ───────────────────────────────────────────────────────────

TEST(SynapticSearch, ThrowsWhenNoIndex) {
    auto brain = makeBrain(5, /*build_index=*/false);
    audio::adapter::search::SynapticSearch search;
    const audio::SearchParams params;
    std::vector<double> usages(brain->size(), 0.0);
    const auto& target = brain->blocks()[2].analysis;
    audio::SearchContext ctx{*brain, target, params, 0, usages};
    EXPECT_THROW((void)search.search(ctx), std::runtime_error);
}

TEST(SynapticSearch, ReturnsValidIndexWithIndex) {
    auto brain = makeBrain(5, /*build_index=*/true);
    audio::adapter::search::SynapticSearch search;
    const audio::SearchParams params;
    std::vector<double> usages(brain->size(), 0.0);
    const auto& target = brain->blocks()[2].analysis;
    audio::SearchContext ctx{*brain, target, params, 2, usages};
    const std::size_t idx = search.search(ctx);
    EXPECT_LT(idx, brain->size());
}

TEST(SynapticSearch, ResultIsANeighbourOfCurrentBlock) {
    // With k=4, every block has all other 4 blocks as precomputed neighbours.
    // The search must always return a valid block index.
    auto brain = makeBrain(5, /*build_index=*/true);
    audio::adapter::search::SynapticSearch search(4);
    const audio::SearchParams params;

    for (std::size_t cur = 0; cur < brain->size(); ++cur) {
        std::vector<double> usages(brain->size(), 0.0);
        const auto& target = brain->blocks()[cur].analysis;
        audio::SearchContext ctx{*brain, target, params, cur, usages};
        const std::size_t result = search.search(ctx);
        EXPECT_LT(result, brain->size());
    }
}
