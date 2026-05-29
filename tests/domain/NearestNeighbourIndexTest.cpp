#include "domain/NearestNeighbourIndex.h"
#include "gtest/gtest.h"

#include <cmath>
#include <vector>

namespace {

// Simple 1-D Euclidean distance.
double euclidean(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0.0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const double d = a[i] - b[i];
        s += d * d;
    }
    return std::sqrt(s);
}

// Build an index from a sorted set of 1-D points.
audio::NearestNeighbourIndex makeIndex(const std::vector<double>& pts, std::size_t k) {
    std::vector<std::vector<double>> points;
    points.reserve(pts.size());
    for (const double v : pts) {
        points.push_back({v});
    }
    audio::NearestNeighbourIndex idx;
    idx.build(std::move(points), euclidean, k);
    return idx;
}

}  // namespace

// ─── Empty state ──────────────────────────────────────────────────────────────

TEST(NearestNeighbourIndex, DefaultIsEmpty) {
    audio::NearestNeighbourIndex idx;
    EXPECT_TRUE(idx.empty());
    EXPECT_EQ(idx.size(), 0u);
}

TEST(NearestNeighbourIndex, NeighborsOnEmptyIndexReturnsEmptySpan) {
    audio::NearestNeighbourIndex idx;
    EXPECT_TRUE(idx.neighbors(0).empty());
}

// ─── After build ──────────────────────────────────────────────────────────────

TEST(NearestNeighbourIndex, SizeMatchesPointCount) {
    auto idx = makeIndex({0.0, 1.0, 2.0, 3.0, 4.0}, 2);
    EXPECT_FALSE(idx.empty());
    EXPECT_EQ(idx.size(), 5u);
}

// ─── kNearest() ───────────────────────────────────────────────────────────────

TEST(NearestNeighbourIndex, KNearestReturnsSingleNearestNeighbour) {
    // Points: 0, 1, 2, 3, 4 — query 0.1 → closest is point 0 (index 0).
    auto idx = makeIndex({0.0, 1.0, 2.0, 3.0, 4.0}, 2);
    const auto nn = idx.kNearest({0.1}, 1);
    ASSERT_EQ(nn.size(), 1u);
    EXPECT_EQ(nn[0], 0u);  // point {0.0} is at index 0
}

TEST(NearestNeighbourIndex, KNearestReturnsSortedAscendingByDistance) {
    auto idx = makeIndex({0.0, 1.0, 2.0, 3.0, 4.0}, 4);
    // Query near point 2.0: expected order 2, then 1 or 3, ...
    const auto nn = idx.kNearest({2.1}, 3);
    ASSERT_EQ(nn.size(), 3u);
    // First result must be index 2 (closest).
    EXPECT_EQ(nn[0], 2u);
}

TEST(NearestNeighbourIndex, KNearestCappedAtIndexSize) {
    // Requesting k > n returns at most n results.
    auto idx = makeIndex({0.0, 1.0, 2.0}, 2);
    const auto nn = idx.kNearest({0.5}, 100);
    EXPECT_LE(nn.size(), 3u);
}

TEST(NearestNeighbourIndex, KNearestAllResultsWithinBounds) {
    auto idx = makeIndex({0.0, 1.0, 2.0, 3.0}, 3);
    const auto nn = idx.kNearest({1.5}, 4);
    for (const auto i : nn) {
        EXPECT_LT(i, 4u);
    }
}

// ─── neighbors() ─────────────────────────────────────────────────────────────

TEST(NearestNeighbourIndex, PrecomputedNeighboursExcludeSelf) {
    auto idx = makeIndex({0.0, 1.0, 2.0, 3.0, 4.0}, 4);
    for (std::size_t i = 0; i < 5; ++i) {
        const auto nbrs = idx.neighbors(i);
        for (const auto n : nbrs) {
            EXPECT_NE(n, i) << "Block " << i << " has itself as a neighbour";
        }
    }
}

TEST(NearestNeighbourIndex, PrecomputedNeighboursCountCappedAtK) {
    auto idx = makeIndex({0.0, 1.0, 2.0, 3.0, 4.0}, 2);
    for (std::size_t i = 0; i < 5; ++i) {
        const auto nbrs = idx.neighbors(i);
        EXPECT_LE(nbrs.size(), 2u);
    }
}

TEST(NearestNeighbourIndex, NeighboursOutOfRangeReturnsEmptySpan) {
    auto idx = makeIndex({0.0, 1.0, 2.0}, 2);
    EXPECT_TRUE(idx.neighbors(99).empty());
}

TEST(NearestNeighbourIndex, SinglePointIndexHasNoNeighbours) {
    auto idx = makeIndex({42.0}, 1);
    EXPECT_EQ(idx.size(), 1u);
    // No other points → no precomputed neighbours.
    EXPECT_TRUE(idx.neighbors(0).empty());
}
