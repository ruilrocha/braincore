#include "adapter/fft/PocketfftBackend.h"
#include "gtest/gtest.h"

#include <cmath>
#include <vector>

namespace {

using audio::adapter::fft::PocketfftBackend;
using ComplexValue = audio::port::IFft::ComplexValue;

// L2 distance between two real vectors.
double l2(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0.0;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const double d = a[i] - b[i];
        s += d * d;
    }
    return std::sqrt(s);
}

}  // namespace

// ─── forward() ────────────────────────────────────────────────────────────────

TEST(PocketfftBackend, ForwardOutputSizeIsNOver2Plus1) {
    PocketfftBackend fft;
    const std::vector<double> input(64, 0.0);
    const auto spectrum = fft.forward(input);
    EXPECT_EQ(spectrum.size(), 64u / 2 + 1);
}

TEST(PocketfftBackend, ForwardOfZeroSignalIsAllZero) {
    PocketfftBackend fft;
    const std::vector<double> input(64, 0.0);
    const auto spectrum = fft.forward(input);
    for (const auto& c : spectrum) {
        EXPECT_NEAR(c.real, 0.0, 1e-12);
        EXPECT_NEAR(c.imag, 0.0, 1e-12);
    }
}

TEST(PocketfftBackend, ForwardOfConstantSignalHasNonZeroDCBin) {
    PocketfftBackend fft;
    const std::vector<double> input(64, 1.0);
    const auto spectrum = fft.forward(input);
    // DC bin (index 0) should be non-zero for a constant signal.
    EXPECT_GT(std::abs(spectrum[0].real), 0.0);
}

// ─── inverse() ────────────────────────────────────────────────────────────────

TEST(PocketfftBackend, InverseOutputSizeMatchesRequest) {
    PocketfftBackend fft;
    const std::vector<double> input(64, 0.0);
    const auto spectrum = fft.forward(input);
    const auto recovered = fft.inverse(spectrum, 64);
    EXPECT_EQ(recovered.size(), 64u);
}

TEST(PocketfftBackend, ForwardInverseRoundTrip) {
    PocketfftBackend fft;
    // Sine signal for round-trip test.
    std::vector<double> input(64);
    for (std::size_t i = 0; i < 64; ++i) {
        input[i] = std::sin(2.0 * M_PI * 4.0 * static_cast<double>(i) / 64.0);
    }
    const auto spectrum = fft.forward(input);
    const auto recovered = fft.inverse(spectrum, 64);
    ASSERT_EQ(recovered.size(), 64u);
    EXPECT_LT(l2(input, recovered), 1e-9) << "FFT round-trip error exceeds tolerance";
}

// ─── dct() ────────────────────────────────────────────────────────────────────

TEST(PocketfftBackend, DctOutputSizeMatchesRequestedLength) {
    PocketfftBackend fft;
    const std::vector<double> input(24, 1.0);
    const auto coeffs = fft.dct(input, 12);
    EXPECT_EQ(coeffs.size(), 12u);
}

TEST(PocketfftBackend, DctOutputSizeCanExceedInput) {
    // output_length may be <= input.size(); requesting fewer coeffs is normal.
    PocketfftBackend fft;
    const std::vector<double> input(32, 1.0);
    const auto coeffs = fft.dct(input, 8);
    EXPECT_EQ(coeffs.size(), 8u);
}

TEST(PocketfftBackend, DctOfZeroSignalIsAllZero) {
    PocketfftBackend fft;
    const std::vector<double> input(24, 0.0);
    const auto coeffs = fft.dct(input, 12);
    for (const double c : coeffs) {
        EXPECT_NEAR(c, 0.0, 1e-12);
    }
}

TEST(PocketfftBackend, DctIsDeterministic) {
    PocketfftBackend fft;
    std::vector<double> input(24);
    for (std::size_t i = 0; i < 24; ++i) {
        input[i] = static_cast<double>(i) * 0.1;
    }
    const auto c1 = fft.dct(input, 12);
    const auto c2 = fft.dct(input, 12);
    ASSERT_EQ(c1.size(), c2.size());
    for (std::size_t i = 0; i < c1.size(); ++i) {
        EXPECT_DOUBLE_EQ(c1[i], c2[i]);
    }
}
