#include "domain/BlockConfig.h"
#include "domain/WindowFunction.h"
#include "gtest/gtest.h"

#include <cmath>
#include <numbers>
#include <vector>

using audio::WindowFunction;
using audio::WindowShape;

// ─── apply() ──────────────────────────────────────────────────────────────────

TEST(WindowFunction, RectangleIsIdentity) {
    std::vector samples = {1.0, 2.0, 3.0, 4.0};
    const auto original = samples;
    WindowFunction::apply(samples, WindowShape::Rectangle);
    EXPECT_EQ(samples, original);
}

TEST(WindowFunction, EmptySamplesDoesNotCrash) {
    std::vector<double> empty;
    EXPECT_NO_THROW(WindowFunction::apply(empty, WindowShape::Hann));
    EXPECT_NO_THROW(WindowFunction::apply(empty, WindowShape::Hamming));
}

TEST(WindowFunction, HannFirstAndLastSampleNearZero) {
    // Hann window: w(0) = 0, w(N-1) = 0
    std::vector samples(64, 1.0);
    WindowFunction::apply(samples, WindowShape::Hann);
    EXPECT_NEAR(samples.front(), 0.0, 1e-10);
    EXPECT_NEAR(samples.back(), 0.0, 1e-10);
}

TEST(WindowFunction, HannMidpointNearOne) {
    // Hann window peaks at 1.0 at the midpoint for even-length arrays
    std::vector samples(65, 1.0);  // odd length: exact midpoint
    WindowFunction::apply(samples, WindowShape::Hann);
    // mid index = 32, w(32) = 0.5*(1 - cos(2π*32/64)) = 0.5*(1 - cos(π)) = 1.0
    EXPECT_NEAR(samples[32], 1.0, 1e-10);
}

TEST(WindowFunction, HammingMidpointNearOne) {
    // Hamming: w(n) = 0.53836 - 0.46164*cos(2π*n/(N-1))
    // At n = (N-1)/2: cos(π) = -1 → w = 0.53836 + 0.46164 = 1.0
    std::vector samples(65, 1.0);
    WindowFunction::apply(samples, WindowShape::Hamming);
    EXPECT_NEAR(samples[32], 1.0, 1e-10);
}

TEST(WindowFunction, BlackmanEdgesNearZero) {
    std::vector samples(64, 1.0);
    WindowFunction::apply(samples, WindowShape::Blackman);
    EXPECT_NEAR(samples.front(), 0.0, 1e-10);
    EXPECT_NEAR(samples.back(), 0.0, 1e-10);
}

TEST(WindowFunction, BartlettEdgesZero) {
    std::vector samples(64, 1.0);
    WindowFunction::apply(samples, WindowShape::Bartlett);
    EXPECT_NEAR(samples.front(), 0.0, 1e-10);
    EXPECT_NEAR(samples.back(), 0.0, 1e-10);
}

TEST(WindowFunction, GaussianPeaksAtMidpoint) {
    std::vector samples(65, 1.0);
    WindowFunction::apply(samples, WindowShape::Gaussian);
    // Gaussian peaks at centre; flanks should be smaller
    EXPECT_GT(samples[32], samples[0]);
    EXPECT_GT(samples[32], samples[64]);
}

// ─── normalise() ──────────────────────────────────────────────────────────────

TEST(WindowFunction, NormaliseEmptyDoesNotCrash) {
    std::vector<double> empty;
    EXPECT_NO_THROW(WindowFunction::normalise(empty));
}

TEST(WindowFunction, NormaliseSilentBlockUnchanged) {
    // All zeros: peak = 0, normalise should return early without NaN/inf
    std::vector silent(64, 0.0);
    WindowFunction::normalise(silent);
    for (const double s : silent) {
        EXPECT_EQ(s, 0.0);
    }
}

TEST(WindowFunction, NormaliseScalesPeakToOne) {
    std::vector samples = {0.0, 0.5, 1.0, -0.5};
    WindowFunction::normalise(samples);
    double peak = 0.0;
    for (const double s : samples) {
        peak = std::max(peak, std::fabs(s));
    }
    EXPECT_NEAR(peak, 1.0, 1e-10);
}

TEST(WindowFunction, NormaliseRemovesDC) {
    // Uniform signal: all same value → should be centred on zero after normalise
    std::vector samples(4, 3.0);
    WindowFunction::normalise(samples);
    // After DC removal the signal is still all-zero (peak = 0 → early return)
    for (const double s : samples) {
        EXPECT_EQ(s, 0.0);
    }
}
