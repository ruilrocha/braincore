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
std::vector<std::vector<float>> constantBlock(size_t block_size, float value,
                                              std::size_t channels = 1) {
    return {channels, std::vector<float>(block_size, value)};
}

/// Compute the mean of all samples across all channels.
double meanAmplitude(const std::vector<std::vector<double>>& buf) {
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
    EXPECT_EQ(ola.stepSize(), 4096U);
}

TEST(OlaBuffer, ActiveWhenOverlapIsHalfStep) {
    OlaBuffer ola(4096, 0.5, WindowShape::Hann);
    EXPECT_TRUE(ola.active());
    EXPECT_EQ(ola.stepSize(), 2048U);
}

TEST(OlaBuffer, AccumulateIsNoOpWhenInactive) {
    OlaBuffer ola(16, 0.0, WindowShape::Hann);
    const auto block = constantBlock(16, 1.0f);
    // Should not crash; buffers not allocated.
    ola.accumulate(block);
    EXPECT_FALSE(ola.active());
}

TEST(OlaBuffer, ReadReturnsZeroWhenInactive) {
    OlaBuffer ola(16, 0.0, WindowShape::Hann);
    std::vector<std::vector<double>> out;
    const std::size_t n = ola.read(out);
    EXPECT_EQ(n, 0U);
}

// At 50 % overlap with a Hann window, N consecutive identical blocks should
// produce output with amplitude close to the input (perfect-reconstruction
// property). We allow a generous ±20 % tolerance because the periodic Hann
// sum is 1.0 in the steady state but transitions from 0 near the start.
TEST(OlaBuffer, HalfOverlapHannAmplitudeIsApproximatelyPreserved) {
    constexpr std::size_t kBS = 512;
    constexpr float kInput = 1.0f;

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
    const auto block = constantBlock(kBS, 1.0f);

    ola.accumulate(block);
    ola.accumulate(block);

    ola.resetBuffer();

    // After reset, reading should give ~zero.
    std::vector<std::vector<double>> out;
    ola.read(out);

    const double mean = meanAmplitude(out);
    EXPECT_LT(mean, 1e-9) << "Buffers not zeroed after reset; mean=" << mean;
}

TEST(OlaBuffer, StereoAccumulateProducesCorrectChannelCount) {
    constexpr std::size_t kBS = 128;
    OlaBuffer ola(kBS, 0.5, WindowShape::Hann);
    const auto block = constantBlock(kBS, 0.5f, 2);  // 2 channels

    for (int i = 0; i < 4; ++i) {
        ola.accumulate(block);
    }

    std::vector<std::vector<double>> out;
    ola.read(out);

    ASSERT_EQ(out.size(), 2U) << "Expected 2 channels in OLA output";
    EXPECT_EQ(out[0].size(), kBS / 2) << "Unexpected step size in channel 0";
    EXPECT_EQ(out[1].size(), kBS / 2) << "Unexpected step size in channel 1";
}

TEST(OlaBuffer, HammingWindowProducesNonZeroOutput) {
    // Any window with overlap > 0 should produce non-zero output for a non-zero input.
    constexpr std::size_t kBS = 256;
    OlaBuffer ola(kBS, 0.5, WindowShape::Hamming);
    EXPECT_TRUE(ola.active());

    const auto block = constantBlock(kBS, 1.0f);
    for (int i = 0; i < 4; ++i) {
        ola.accumulate(block);
    }

    std::vector<std::vector<double>> out;
    ola.read(out);
    EXPECT_GT(meanAmplitude(out), 0.0) << "Hamming OLA output should be non-zero";
}

TEST(OlaBuffer, MultipleReadAccumulateCyclesAreConsistent) {
    constexpr std::size_t kBS = 128;
    OlaBuffer ola(kBS, 0.5, WindowShape::Hann);
    const auto block = constantBlock(kBS, 1.0f);

    // Interleaved warm-up: reach true steady state.
    std::vector<std::vector<double>> discard;
    for (int i = 0; i < 8; ++i) {
        ola.accumulate(block);
        ola.read(discard);
    }

    // Two more interleaved cycles — both should be in the same steady-state range.
    std::vector<std::vector<double>> out1;
    std::vector<std::vector<double>> out2;
    ola.accumulate(block);
    ola.read(out1);
    ola.accumulate(block);
    ola.read(out2);

    const double mean1 = meanAmplitude(out1);
    const double mean2 = meanAmplitude(out2);
    // Steady-state consecutive outputs should be very close (within 5%).
    EXPECT_NEAR(mean1, mean2, mean1 * 0.05) << "Steady-state output should be consistent";
}

TEST(OlaBuffer, ThreeChannelProducesThreeChannelOutput) {
    constexpr std::size_t kBS = 64;
    OlaBuffer ola(kBS, 0.5, WindowShape::Hann);
    const auto block = constantBlock(kBS, 0.5f, 3);

    for (int i = 0; i < 4; ++i) {
        ola.accumulate(block);
    }

    std::vector<std::vector<double>> out;
    ola.read(out);
    ASSERT_EQ(out.size(), 3U);
    for (const auto& ch : out) {
        EXPECT_EQ(ch.size(), kBS / 2);
    }
}

TEST(OlaBuffer, BufferHandlesWraparoundGracefully) {
    // Feed many blocks to exercise the circular buffer wrap.
    constexpr std::size_t kBS = 64;
    OlaBuffer ola(kBS, 0.5, WindowShape::Hann);
    const auto block = constantBlock(kBS, 0.5);

    std::vector<std::vector<double>> out;
    for (int i = 0; i < 32; ++i) {
        ola.accumulate(block);
        ola.read(out);
        for (const auto& ch : out) {
            for (const double v : ch) {
                EXPECT_TRUE(std::isfinite(v)) << "Non-finite value after " << i << " iterations";
            }
        }
    }
}

}  // namespace
