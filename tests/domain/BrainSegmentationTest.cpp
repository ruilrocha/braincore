#include "domain/AudioPrint.h"
#include "domain/BlockConfig.h"
#include "domain/Brain.h"
#include "domain/Sound.h"
#include "domain/port/IAnalyser.h"
#include "domain/port/ISearchStrategy.h"
#include "gtest/gtest.h"

#include <cmath>
#include <memory>
#include <vector>

namespace {

// Minimal stub analyser: returns a constant fingerprint, Euclidean distance.
class StubAnalyser final : public audio::port::IAnalyser {
public:
    [[nodiscard]] std::vector<double> compute(const std::vector<double>& /*block*/,
                                              int /*sr*/) const override {
        return {1.0, 0.0};
    }

    [[nodiscard]] audio::AudioPrint analyse(const std::vector<double>& /*block*/,
                                            int /*sr*/) const override {
        audio::AudioPrint p;
        p.mfcc = {1.0, 0.0};
        p.spectral = {0.5};
        p.normalised_mfcc = {1.0, 0.0};
        p.normalised_spectral = {0.5};
        p.dominant_freq = 440.0;
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

// Minimal stub search strategy: always returns index 0.
class StubSearch final : public audio::port::ISearchStrategy {
public:
    [[nodiscard]] std::size_t search(const std::vector<double>& /*target_fp*/,
                                     std::vector<audio::Block>& /*blocks*/,
                                     const audio::port::IAnalyser& /*analyser*/,
                                     const audio::SearchParams& /*params*/,
                                     std::size_t /*current_idx*/) const override {
        return 0;
    }
};

// Helper: create a mono Sound of N silence samples at 44100 Hz.
audio::Sound silentMono(int num_samples, int sample_rate = 44100) {
    return audio::Sound({audio::Channel(num_samples, 0.0)}, sample_rate);
}

}  // namespace

// ─── Block count ──────────────────────────────────────────────────────────────

TEST(BrainSegmentation, ExactMultipleProducesCorrectBlockCount) {
    // 4 blocks of 4 samples each, no overlap.
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 0, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), std::make_shared<StubSearch>(), cfg);
    brain.addSound(silentMono(16));
    EXPECT_EQ(brain.size(), 4U);
}

TEST(BrainSegmentation, TrailingPartialBlockIsPadded) {
    // 17 samples / block_size 4 → 5 blocks (last padded with zeros).
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 0, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), std::make_shared<StubSearch>(), cfg);
    brain.addSound(silentMono(17));
    EXPECT_EQ(brain.size(), 5U);
}

TEST(BrainSegmentation, SingleSampleProducesOneBlock) {
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 0, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), std::make_shared<StubSearch>(), cfg);
    brain.addSound(silentMono(1));
    EXPECT_EQ(brain.size(), 1U);
}

TEST(BrainSegmentation, EmptySoundAddsNoBlocks) {
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 0, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), std::make_shared<StubSearch>(), cfg);
    brain.addSound(audio::Sound({}, 44100));
    EXPECT_TRUE(brain.empty());
}

TEST(BrainSegmentation, OverlapReducesBlockCount) {
    // 8 samples, block_size=4, overlap=2 → step=2 → positions 0,2,4,6 → 4 blocks.
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 2, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), std::make_shared<StubSearch>(), cfg);
    brain.addSound(silentMono(8));
    EXPECT_EQ(brain.size(), 4U);
}

TEST(BrainSegmentation, MultipleSoundsAccumulateBlocks) {
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 0, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), std::make_shared<StubSearch>(), cfg);
    brain.addSound(silentMono(8));   // 2 blocks
    brain.addSound(silentMono(12));  // 3 blocks
    EXPECT_EQ(brain.size(), 5U);
}

TEST(BrainSegmentation, SourcesTrackedCorrectly) {
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 0, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), std::make_shared<StubSearch>(), cfg);
    brain.addSound(silentMono(8), "a.wav");
    brain.addSound(silentMono(8), "b.wav");

    ASSERT_EQ(brain.sources().size(), 2U);
    EXPECT_EQ(brain.sources()[0].filename, "a.wav");
    EXPECT_EQ(brain.sources()[1].filename, "b.wav");
    EXPECT_EQ(brain.sources()[0].num_blocks, 2U);
    EXPECT_EQ(brain.sources()[1].num_blocks, 2U);
}
