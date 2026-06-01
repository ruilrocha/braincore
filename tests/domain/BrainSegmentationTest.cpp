#include "domain/BlockAnalysis.h"
#include "domain/BlockConfig.h"
#include "domain/Brain.h"
#include "domain/Sound.h"
#include "domain/port/IAnalyser.h"
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
        p.mfcc = {1.0f, 0.0f};
        p.spectral = {0.5f};
        p.dominant_freq = 440.0f;
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

// Helper: create a mono Sound of N silence samples at 44100 Hz.
audio::Sound silentMono(int num_samples, int sample_rate = 44100) {
    return audio::Sound({audio::Channel(num_samples, 0.0f)}, sample_rate);
}

}  // namespace

// ─── Block count ──────────────────────────────────────────────────────────────

TEST(BrainSegmentation, ExactMultipleProducesCorrectBlockCount) {
    // 4 blocks of 4 samples each, no overlap.
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 0.0, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(16));
    EXPECT_EQ(brain.size(), 4U);
}

TEST(BrainSegmentation, TrailingPartialBlockIsPadded) {
    // 17 samples / block_size 4 → 5 blocks (last padded with zeros).
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 0.0, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(17));
    EXPECT_EQ(brain.size(), 5U);
}

TEST(BrainSegmentation, SingleSampleProducesOneBlock) {
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 0.0, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(1));
    EXPECT_EQ(brain.size(), 1U);
}

TEST(BrainSegmentation, EmptySoundAddsNoBlocks) {
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 0.0, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(audio::Sound({}, 44100));
    EXPECT_TRUE(brain.empty());
}

TEST(BrainSegmentation, OverlapReducesBlockCount) {
    // 8 samples, block_size=4, overlap=0.5 → step=2 → positions 0,2,4,6 → 4 blocks.
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 0.5, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(8));
    EXPECT_EQ(brain.size(), 4U);
}

TEST(BrainSegmentation, MultipleSoundsAccumulateBlocks) {
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 0.0, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(8));   // 2 blocks
    brain.addSound(silentMono(12));  // 3 blocks
    EXPECT_EQ(brain.size(), 5U);
}

TEST(BrainSegmentation, SourcesTrackedCorrectly) {
    constexpr audio::BlockConfig cfg{
        .block_size = 4, .overlap = 0.0, .window = audio::WindowShape::Rectangle};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(8), "a.wav");
    brain.addSound(silentMono(8), "b.wav");

    ASSERT_EQ(brain.sources().size(), 2U);
    EXPECT_EQ(brain.sources()[0].filename, "a.wav");
    EXPECT_EQ(brain.sources()[1].filename, "b.wav");
    EXPECT_EQ(brain.sources()[0].num_blocks, 2U);
    EXPECT_EQ(brain.sources()[1].num_blocks, 2U);
}

// ─── buildIndex() ─────────────────────────────────────────────────────────────

TEST(BrainSegmentation, HasIndexFalseBeforeBuild) {
    constexpr audio::BlockConfig cfg{.block_size = 4};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(16));
    EXPECT_FALSE(brain.hasIndex());
}

TEST(BrainSegmentation, HasIndexTrueAfterBuild) {
    constexpr audio::BlockConfig cfg{.block_size = 4};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(16));
    brain.buildIndex(2);
    EXPECT_TRUE(brain.hasIndex());
}

TEST(BrainSegmentation, KNearestThrowsWithoutIndex) {
    constexpr audio::BlockConfig cfg{.block_size = 4};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(16));
    EXPECT_THROW((void)brain.kNearest({1.0f, 0.0f}, 1), std::runtime_error);
}

TEST(BrainSegmentation, KNearestReturnsResultsAfterBuild) {
    constexpr audio::BlockConfig cfg{.block_size = 4};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(16));  // 4 blocks
    brain.buildIndex(2);
    const auto nn = brain.kNearest({1.0f, 0.0f}, 2);
    EXPECT_EQ(nn.size(), 2U);
    for (const auto idx : nn) {
        EXPECT_LT(idx, brain.size());
    }
}

// ─── neighbors() ──────────────────────────────────────────────────────────────

TEST(BrainSegmentation, NeighborsEmptySpanWithoutIndex) {
    constexpr audio::BlockConfig cfg{.block_size = 4};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(16));
    EXPECT_TRUE(brain.neighbors(0).empty());
}

TEST(BrainSegmentation, NeighborsNonEmptyAfterBuild) {
    constexpr audio::BlockConfig cfg{.block_size = 4};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(16));  // 4 blocks
    brain.buildIndex(3);
    // Block 0 should have up to 3 neighbours (the other 3 blocks).
    EXPECT_FALSE(brain.neighbors(0).empty());
}

TEST(BrainSegmentation, NeighborsOutOfRangeReturnsEmptySpan) {
    constexpr audio::BlockConfig cfg{.block_size = 4};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(16));  // 4 blocks
    brain.buildIndex(2);
    EXPECT_TRUE(brain.neighbors(999).empty());
}

// ─── activateSound() / isBlockActive() ───────────────────────────────────────

TEST(BrainSegmentation, AllBlocksActiveByDefault) {
    constexpr audio::BlockConfig cfg{.block_size = 4};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(8), "a.wav");  // 2 blocks
    for (std::size_t i = 0; i < brain.size(); ++i) {
        EXPECT_TRUE(brain.isBlockActive(i));
    }
}

TEST(BrainSegmentation, DeactivateSoundMarksItsBlocksInactive) {
    constexpr audio::BlockConfig cfg{.block_size = 4};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(8), "a.wav");  // blocks 0-1
    brain.addSound(silentMono(8), "b.wav");  // blocks 2-3
    brain.activateSound("a.wav", false);
    EXPECT_FALSE(brain.isBlockActive(0));
    EXPECT_FALSE(brain.isBlockActive(1));
    EXPECT_TRUE(brain.isBlockActive(2));
    EXPECT_TRUE(brain.isBlockActive(3));
}

TEST(BrainSegmentation, ReactivateSoundRestoresActiveBlocks) {
    constexpr audio::BlockConfig cfg{.block_size = 4};
    audio::Brain brain(std::make_shared<StubAnalyser>(), cfg);
    brain.addSound(silentMono(8), "a.wav");
    brain.activateSound("a.wav", false);
    EXPECT_FALSE(brain.isBlockActive(0));
    brain.activateSound("a.wav", true);
    EXPECT_TRUE(brain.isBlockActive(0));
}

// ─── rebuild() ────────────────────────────────────────────────────────────────

TEST(BrainSegmentation, RebuildPreservesBlockCount) {
    constexpr audio::BlockConfig cfg{.block_size = 4};
    auto analyser = std::make_shared<StubAnalyser>();
    audio::Brain original(analyser, cfg);
    original.addSound(silentMono(16));  // 4 blocks

    constexpr audio::BlockConfig new_cfg{.block_size = 8};
    auto rebuilt = audio::Brain::rebuild(original.blocks(), analyser, new_cfg);
    EXPECT_EQ(rebuilt->size(), original.size());
    EXPECT_EQ(rebuilt->blockSize(), 8);
}
