#include "adapter/analysis/MfccAnalyser.h"
#include "adapter/fft/PocketfftBackend.h"
#include "gtest/gtest.h"

#include <cmath>
#include <memory>
#include <vector>

namespace {

constexpr int kSampleRate = 44100;
constexpr int kNumMfcc = 12;
constexpr int kNumFftBins = 100;

// Sine wave at 440 Hz, block_size samples.
std::vector<double> sineBlock(std::size_t block_size, double freq = 440.0,
                              double sr = kSampleRate) {
    std::vector<double> v(block_size);
    for (std::size_t i = 0; i < block_size; ++i) {
        v[i] = std::sin(2.0 * M_PI * freq * static_cast<double>(i) / sr);
    }
    return v;
}

// White noise block (seeded, deterministic).
std::vector<double> noiseBlock(std::size_t block_size, double amplitude = 0.5) {
    std::vector<double> v(block_size);
    for (std::size_t i = 0; i < block_size; ++i) {
        v[i] = amplitude * (std::sin(static_cast<double>(i) * 12345.6789) * 0.5 + 0.5 - 0.5);
    }
    return v;
}

std::shared_ptr<audio::adapter::analysis::MfccAnalyser> makeAnalyser() {
    auto fft = std::make_shared<audio::adapter::fft::PocketfftBackend>();
    return std::make_shared<audio::adapter::analysis::MfccAnalyser>(fft, kNumMfcc, kNumFftBins);
}

}  // namespace

// ─── compute() ────────────────────────────────────────────────────────────────

TEST(MfccAnalyser, ComputeOutputSizeMatchesNumMfcc) {
    auto analyser = makeAnalyser();
    const auto block = sineBlock(1024);
    const auto fp = analyser->compute(block, kSampleRate);
    EXPECT_EQ(fp.size(), static_cast<std::size_t>(kNumMfcc));
}

TEST(MfccAnalyser, ComputeIsDeterministic) {
    auto analyser = makeAnalyser();
    const auto block = sineBlock(1024);
    const auto fp1 = analyser->compute(block, kSampleRate);
    const auto fp2 = analyser->compute(block, kSampleRate);
    ASSERT_EQ(fp1.size(), fp2.size());
    for (std::size_t i = 0; i < fp1.size(); ++i) {
        EXPECT_DOUBLE_EQ(fp1[i], fp2[i]);
    }
}

TEST(MfccAnalyser, DifferentBlocksHaveDifferentFingerprints) {
    auto analyser = makeAnalyser();
    const auto fp_sine = analyser->compute(sineBlock(1024, 440.0), kSampleRate);
    const auto fp_noise = analyser->compute(noiseBlock(1024), kSampleRate);
    double diff = 0.0;
    for (std::size_t i = 0; i < fp_sine.size() && i < fp_noise.size(); ++i) {
        diff += std::abs(fp_sine[i] - fp_noise[i]);
    }
    EXPECT_GT(diff, 1e-6) << "Sine and noise should produce distinct MFCC fingerprints";
}

// ─── analyse() ────────────────────────────────────────────────────────────────

TEST(MfccAnalyser, AnalyseMfccSizeMatchesNumMfcc) {
    auto analyser = makeAnalyser();
    const auto print = analyser->analyse(sineBlock(1024), kSampleRate);
    EXPECT_EQ(print.mfcc.size(), static_cast<std::size_t>(kNumMfcc));
}

TEST(MfccAnalyser, AnalyseSpectralSizeMatchesNumFftBins) {
    auto analyser = makeAnalyser();
    const auto print = analyser->analyse(sineBlock(1024), kSampleRate);
    EXPECT_EQ(print.spectral.size(), static_cast<std::size_t>(kNumFftBins));
}

TEST(MfccAnalyser, AnalyseMfccMatchesCompute) {
    // analyse().mfcc must be identical to compute() for the same block.
    auto analyser = makeAnalyser();
    const auto block = sineBlock(1024);
    const auto from_compute = analyser->compute(block, kSampleRate);
    const auto from_analyse = analyser->analyse(block, kSampleRate).mfcc;
    ASSERT_EQ(from_compute.size(), from_analyse.size());
    for (std::size_t i = 0; i < from_compute.size(); ++i) {
        EXPECT_DOUBLE_EQ(from_compute[i], from_analyse[i]);
    }
}

// ─── distance() ───────────────────────────────────────────────────────────────

TEST(MfccAnalyser, DistanceSelfIsZero) {
    auto analyser = makeAnalyser();
    const auto fp = analyser->compute(sineBlock(1024), kSampleRate);
    EXPECT_NEAR(analyser->distance(fp, fp), 0.0, 1e-12);
}

TEST(MfccAnalyser, DistanceIsSymmetric) {
    auto analyser = makeAnalyser();
    const auto fa = analyser->compute(sineBlock(1024, 440.0), kSampleRate);
    const auto fb = analyser->compute(sineBlock(1024, 880.0), kSampleRate);
    EXPECT_NEAR(analyser->distance(fa, fb), analyser->distance(fb, fa), 1e-12);
}

TEST(MfccAnalyser, DistanceIsNonNegative) {
    auto analyser = makeAnalyser();
    const auto fa = analyser->compute(sineBlock(1024, 440.0), kSampleRate);
    const auto fb = analyser->compute(sineBlock(1024, 220.0), kSampleRate);
    EXPECT_GE(analyser->distance(fa, fb), 0.0);
}

TEST(MfccAnalyser, CloserBlocksHaveSmallerDistance) {
    auto analyser = makeAnalyser();
    // 440 Hz and 442 Hz (almost identical) should be closer than 440 Hz and 2000 Hz.
    const auto fa = analyser->compute(sineBlock(1024, 440.0), kSampleRate);
    const auto fb = analyser->compute(sineBlock(1024, 442.0), kSampleRate);
    const auto fc = analyser->compute(sineBlock(1024, 2000.0), kSampleRate);
    EXPECT_LT(analyser->distance(fa, fb), analyser->distance(fa, fc));
}
