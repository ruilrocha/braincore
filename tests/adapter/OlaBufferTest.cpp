#include "adapter/effects/OlaBuffer.h"
#include "domain/WindowShape.h"
#include "gtest/gtest.h"

#include <cmath>
#include <numeric>
#include <vector>

namespace {

using audio::WindowShape;
using audio::adapter::effects::OlaBuffer;

// ── Helpers ────────────────────────────────────────────────────────────────────

/// Make a single-channel block filled with a constant value.
static std::vector<std::vector<double>> constantBlock(std::size_t block_size, double value,
                                                      int channels = 1) {
    return std::vector<std::vector<double>>(channels, std::vector<double>(block_size, value));
}

/// Compute the mean of all samples across all channels.
static double meanAmplitude(const std::vector<std::vector<double>>& buf) {
    double sum = 0.0;
    std::size_t count = 0;
    for (const auto& ch : buf) {
        for (const double v : ch) {
            sum += std::fabs(v);
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

// ── Tests ──────────────────────────────────────────────────────────────────────

TEST(OlaBuffer, InactiveWhenOverlapIsZero) {
    OlaBuffer ola(4096, 0.0, WindowShape::Hann);
    EXPECT_FALSE(ola.active());
    EXPECT_EQ(ola.stepSize(), 4096u);
}

TEST(OlaBuffer, ActiveWhenOverlapIsHalfStep) {
    OlaBuffer ola(4096, 0.5, WindowShape::Hann);
    EXPECT_TRUE(ola.active());
    EXPECT_EQ(ola.stepSize(), 2048u);
}

TEST(OlaBuffer, AccumulateIsNoOpWhenInactive) {
    OlaBuffer ola(16, 0.0, WindowShape::Hann);
    const auto block = constantBlock(16, 1.0);
    // Should not crash; buffers not allocated.
    ola.accumulate(block);
    EXPECT_FALSE(ola.active());
}

TEST(OlaBuffer, ReadReturnsZeroWhenInactive) {
    OlaBuffer ola(16, 0.0, WindowShape::Hann);
    std::vector<std::vector<double>> out;
    const std::size_t n = ola.read(out);
    EXPECT_EQ(n, 0u);
}

// At 50 % overlap with a Hann window, N consecutive identical blocks should
// produce output with amplitude close to the input (perfect-reconstruction
// property). We allow a generous ±20 % tolerance because the periodic Hann
// sum is 1.0 in the steady state but transitions from 0 near the start.
TEST(OlaBuffer, HalfOverlapHannAmplitudeIsApproximatelyPreserved) {
    constexpr std::size_t kBS = 512;
    constexpr double kInput = 1.0;

    OlaBuffer ola(kBS, 0.5, WindowShape::Hann);
    const auto block = constantBlock(kBS, kInput);

    // Feed 6 blocks to reach steady state (first few are ramp-up).
    for (int i = 0; i < 6; ++i) {
        ola.accumulate(block);
    }

    // Read one step worth of output (steady-state).
    std::vector<std::vector<double>> out;
    ola.read(out);

    const double mean = meanAmplitude(out);
    // Hann window sums to ~1.0 at 50 % overlap in steady state.
    EXPECT_GT(mean, 0.3) << "OLA amplitude too low; mean=" << mean;
    EXPECT_LT(mean, 1.5) << "OLA amplitude too high; mean=" << mean;
}

TEST(OlaBuffer, ResetZerosBuffersAndCursors) {
    constexpr std::size_t kBS = 64;
    OlaBuffer ola(kBS, 0.5, WindowShape::Hann);
    const auto block = constantBlock(kBS, 1.0);

    ola.accumulate(block);
    ola.accumulate(block);

    ola.reset();

    // After reset, reading should give ~zero.
    std::vector<std::vector<double>> out;
    ola.read(out);

    const double mean = meanAmplitude(out);
    EXPECT_LT(mean, 1e-9) << "Buffers not zeroed after reset; mean=" << mean;
}

TEST(OlaBuffer, StereoAccumulateProducesCorrectChannelCount) {
    constexpr std::size_t kBS = 128;
    OlaBuffer ola(kBS, 0.5, WindowShape::Hann);
    const auto block = constantBlock(kBS, 0.5, 2);  // 2 channels

    for (int i = 0; i < 4; ++i) {
        ola.accumulate(block);
    }

    std::vector<std::vector<double>> out;
    ola.read(out);

    ASSERT_EQ(out.size(), 2u) << "Expected 2 channels in OLA output";
    EXPECT_EQ(out[0].size(), kBS / 2) << "Unexpected step size in channel 0";
    EXPECT_EQ(out[1].size(), kBS / 2) << "Unexpected step size in channel 1";
}

}  // namespace
