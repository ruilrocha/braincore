#include "adapter/effects/EffectHelpers.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace {

// Constant block of @p value, @p n samples.
std::vector<double> constant(std::size_t n, double value = 1.0) {
    return std::vector<double>(n, value);
}

// Linearly ramped block: 0 → 1.
std::vector<double> ramp(std::size_t n) {
    std::vector<double> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<double>(i) / static_cast<double>(n - 1);
    }
    return v;
}

double rms(const std::vector<double>& v) {
    double s = 0.0;
    for (double x : v)
        s += x * x;
    return std::sqrt(s / static_cast<double>(v.size()));
}

}  // namespace

// ─── applyGrainEnvelope ───────────────────────────────────────────────────────

TEST(EffectHelpers, GrainEnvelopeFirstSampleNearZero) {
    auto grain = constant(128, 1.0);
    audio::effects::applyGrainEnvelope(grain);
    // Hann window: first and last samples should be at or near 0.
    EXPECT_NEAR(grain.front(), 0.0, 0.01);
}

TEST(EffectHelpers, GrainEnvelopeLastSampleNearZero) {
    auto grain = constant(128, 1.0);
    audio::effects::applyGrainEnvelope(grain);
    EXPECT_NEAR(grain.back(), 0.0, 0.01);
}

TEST(EffectHelpers, GrainEnvelopeMidpointIsNearOne) {
    // For a Hann window, the midpoint should be 1.0.
    auto grain = constant(128, 1.0);
    audio::effects::applyGrainEnvelope(grain);
    EXPECT_NEAR(grain[64], 1.0, 0.02);
}

TEST(EffectHelpers, GrainEnvelopePreservesSize) {
    auto grain = constant(256, 0.5);
    audio::effects::applyGrainEnvelope(grain);
    EXPECT_EQ(grain.size(), 256u);
}

// ─── extractGrain ─────────────────────────────────────────────────────────────

TEST(EffectHelpers, ExtractGrainOutputSizeMatchesRequest) {
    const auto src = constant(512, 1.0);
    const auto grain = audio::effects::extractGrain(src, 64, 0);
    EXPECT_EQ(grain.size(), 64u);
}

TEST(EffectHelpers, ExtractGrainIsEnveloped) {
    // Extracted grain should have Hann envelope: first and last samples ≈ 0.
    const auto src = constant(512, 1.0);
    const auto grain = audio::effects::extractGrain(src, 64, 0);
    EXPECT_NEAR(grain.front(), 0.0, 0.01);
    EXPECT_NEAR(grain.back(), 0.0, 0.01);
}

TEST(EffectHelpers, ExtractGrainWrapAround) {
    // Offset near the end of the source should wrap without crashing.
    const auto src = constant(64, 1.0);
    EXPECT_NO_THROW({
        const auto grain = audio::effects::extractGrain(src, 32, 50);
        EXPECT_EQ(grain.size(), 32u);
    });
}

// ─── granularScatter ──────────────────────────────────────────────────────────

TEST(EffectHelpers, GranularScatterOutputSizeEqualsBlockSize) {
    const auto src = constant(256, 0.8);
    constexpr std::size_t block_size = 256;
    const auto out = audio::effects::granularScatter(src, block_size, 0.5, 0.0, 1.0);
    EXPECT_EQ(out.size(), block_size);
}

TEST(EffectHelpers, GranularScatterNoScatterNoVariation) {
    // With scatter=0, size/amp/pitch/hop variations=0, output should be non-zero
    // (grains still present) and have the correct size.
    const auto src = constant(256, 1.0);
    const auto out = audio::effects::granularScatter(src, 256, 0.25, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0);
    EXPECT_EQ(out.size(), 256u);
    const double r = rms(out);
    EXPECT_GT(r, 0.0) << "Output should contain audio";
}

// ─── applyStutter ─────────────────────────────────────────────────────────────

TEST(EffectHelpers, ApplyStutterZeroChanceNoChange) {
    auto samples = ramp(128);
    const auto original = samples;
    audio::effects::applyStutter(samples, 0.0, 2);
    EXPECT_EQ(samples, original);
}

TEST(EffectHelpers, ApplyStutterPreservesSize) {
    auto samples = constant(128, 0.5);
    audio::effects::applyStutter(samples, 1.0, 4);
    EXPECT_EQ(samples.size(), 128u);
}

// ─── applyEnvelope ────────────────────────────────────────────────────────────

TEST(EffectHelpers, ApplyEnvelopeShapeNoneNoChange) {
    auto samples = constant(128, 0.7);
    const auto original = samples;
    audio::effects::applyEnvelope(samples, /*shape=*/0, /*amount=*/1.0);
    EXPECT_EQ(samples, original);
}

TEST(EffectHelpers, ApplyEnvelopeShapeDecayFirstHalfLouderThanSecond) {
    auto samples = constant(128, 1.0);
    audio::effects::applyEnvelope(samples, /*shape=*/1, /*amount=*/1.0);
    const double first_rms = rms(std::vector<double>(samples.begin(), samples.begin() + 64));
    const double second_rms = rms(std::vector<double>(samples.begin() + 64, samples.end()));
    EXPECT_GT(first_rms, second_rms) << "Decay: first half should be louder";
}

TEST(EffectHelpers, ApplyEnvelopeShapeSwellSecondHalfLouderThanFirst) {
    auto samples = constant(128, 1.0);
    audio::effects::applyEnvelope(samples, /*shape=*/2, /*amount=*/1.0);
    const double first_rms = rms(std::vector<double>(samples.begin(), samples.begin() + 64));
    const double second_rms = rms(std::vector<double>(samples.begin() + 64, samples.end()));
    EXPECT_LT(first_rms, second_rms) << "Swell: second half should be louder";
}

TEST(EffectHelpers, ApplyEnvelopeShapeTremoloPreservesSize) {
    auto samples = constant(256, 1.0);
    audio::effects::applyEnvelope(samples, /*shape=*/3, /*amount=*/1.0);
    EXPECT_EQ(samples.size(), 256u);
}

TEST(EffectHelpers, ApplyEnvelopeShapePluckPreservesSize) {
    auto samples = constant(256, 1.0);
    audio::effects::applyEnvelope(samples, /*shape=*/4, /*amount=*/1.0);
    EXPECT_EQ(samples.size(), 256u);
}

TEST(EffectHelpers, ApplyEnvelopeAmountZeroNoChange) {
    auto samples = ramp(128);
    const auto original = samples;
    audio::effects::applyEnvelope(samples, /*shape=*/1, /*amount=*/0.0);
    EXPECT_EQ(samples, original) << "amount=0 should be a no-op";
}
