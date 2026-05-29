#include "adapter/effects/SpectralMorph.h"
#include "adapter/fft/PocketfftBackend.h"
#include "gtest/gtest.h"

#include <cmath>
#include <memory>
#include <vector>

namespace {

using audio::adapter::effects::SpectralMorph;
using audio::adapter::fft::PocketfftBackend;

std::shared_ptr<SpectralMorph> makeEffect() {
    auto fft = std::make_shared<PocketfftBackend>();
    return std::make_shared<SpectralMorph>(fft);
}

// Sine wave helper.
std::vector<double> sine(std::size_t n, double freq = 440.0, double sr = 44100.0) {
    std::vector<double> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = std::sin(2.0 * M_PI * freq * static_cast<double>(i) / sr);
    }
    return v;
}

double l2(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0.0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const double d = a[i] - b[i];
        s += d * d;
    }
    return std::sqrt(s);
}

}  // namespace

// ─── SpectralMorph ────────────────────────────────────────────────────────────

TEST(SpectralMorph, OutputSizeMatchesCurrentBlock) {
    auto effect = makeEffect();
    const auto prev = sine(512, 440.0);
    const auto cur = sine(512, 880.0);
    const auto out = effect->apply(prev, cur, 0.5);
    EXPECT_EQ(out.size(), cur.size());
}

TEST(SpectralMorph, AmountZeroReturnsCurrentBlock) {
    // apply(prev, cur, 0.0) should return `cur` (dry — no morphing).
    auto effect = makeEffect();
    const auto prev = sine(512, 440.0);
    const auto cur = sine(512, 880.0);
    const auto out = effect->apply(prev, cur, 0.0);
    ASSERT_EQ(out.size(), cur.size());
    // Tolerance accounts for FFT round-trip.
    EXPECT_LT(l2(out, cur), 1e-6);
}

TEST(SpectralMorph, AmountOneProducesDifferentOutputWhenBlocksDiffer) {
    auto effect = makeEffect();
    const auto prev = sine(512, 220.0);
    const auto cur = sine(512, 880.0);
    const auto out = effect->apply(prev, cur, 1.0);
    ASSERT_EQ(out.size(), cur.size());
    // Full morph of two spectrally different signals should change `cur`.
    EXPECT_GT(l2(out, cur), 1e-6);
}

TEST(SpectralMorph, SameBlockMorphIsNoop) {
    // apply(x, x, any_amount) should be a no-op (spectral midpoint of identical signals =
    // original).
    auto effect = makeEffect();
    const auto block = sine(512, 440.0);
    const auto out = effect->apply(block, block, 1.0);
    ASSERT_EQ(out.size(), block.size());
    EXPECT_LT(l2(out, block), 1e-6);
}

TEST(SpectralMorph, MidAmountOutputIsBetweenDryAndWet) {
    // At amount=0.5, the RMS of the output should be between dry and full-morph.
    auto effect = makeEffect();
    const auto prev = sine(512, 220.0);
    const auto cur = sine(512, 880.0);

    const auto dry = effect->apply(prev, cur, 0.0);
    const auto mid = effect->apply(prev, cur, 0.5);
    const auto wet = effect->apply(prev, cur, 1.0);

    // All outputs should be valid (not NaN / Inf).
    for (std::size_t i = 0; i < cur.size(); ++i) {
        EXPECT_TRUE(std::isfinite(dry[i])) << "dry[" << i << "] is not finite";
        EXPECT_TRUE(std::isfinite(mid[i])) << "mid[" << i << "] is not finite";
        EXPECT_TRUE(std::isfinite(wet[i])) << "wet[" << i << "] is not finite";
    }
    // mid should differ from both extremes.
    EXPECT_GT(l2(mid, dry), 1e-8);
    EXPECT_GT(l2(mid, wet), 1e-8);
}

TEST(SpectralMorph, EmptyInputReturnsEmpty) {
    auto effect = makeEffect();
    const std::vector<double> empty;
    const auto out = effect->apply(empty, empty, 0.5);
    EXPECT_TRUE(out.empty());
}
