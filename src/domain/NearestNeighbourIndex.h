#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <queue>
#include <span>
#include <utility>
#include <vector>

namespace audio {

/**
 * A nearest-neighbour index for a fixed collection of fingerprint vectors.
 *
 * Combines a Vantage-Point tree (for O(log N) dynamic queries) with a
 * precomputed K-NN table (for O(1) synapse traversal).  Both structures
 * are built in a single `build()` call, sharing the same VP tree.
 *
 * ## Roles
 * - **Dynamic query** (`kNearest`): given a novel target fingerprint, returns
 *   the K nearest stored points.  Used by VpTreeSearch.
 * - **Precomputed neighbours** (`neighbors`): for each stored point i, returns
 *   the K indices that were its nearest neighbours at build time.  Used by
 *   SynapticSearch for O(1) neighbourhood access.
 *
 * ## Usage
 * @code
 *   NearestNeighbourIndex idx;
 *   idx.build(fingerprints, dist_fn, 32);    // K = 32 precomputed neighbours
 *   auto nn = idx.kNearest(target_fp, 8);   // O(log N) dynamic query
 *   auto synapses = idx.neighbors(block_i); // O(1) precomputed synapse list
 * @endcode
 *
 * ## Complexity
 * - Build: O(N log N) VP tree construction + O(N·K·log N) precomputation.
 * - kNearest: O(log N) average, O(N) worst case.
 * - neighbors: O(1).
 */
class NearestNeighbourIndex {
public:
    using DistFn = std::function<double(const std::vector<float>&, const std::vector<float>&)>;

    NearestNeighbourIndex() = default;

    NearestNeighbourIndex(const NearestNeighbourIndex&) = delete;
    NearestNeighbourIndex& operator=(const NearestNeighbourIndex&) = delete;
    NearestNeighbourIndex(NearestNeighbourIndex&&) = default;
    NearestNeighbourIndex& operator=(NearestNeighbourIndex&&) = default;

    /**
     * Build the VP tree and precompute K nearest neighbours for each point.
     *
     * @param points  Fingerprint vectors to index (moved in).
     * @param dist    Distance function (metric space: non-negative, symmetric,
     *                triangle inequality).
     * @param k       Number of nearest neighbours to precompute per point.
     */
    void build(std::vector<std::vector<float>> points, DistFn dist, std::size_t k);

    [[nodiscard]] bool empty() const { return items_.empty(); }
    [[nodiscard]] std::size_t size() const { return items_.size(); }

    /**
     * Return up to @p k nearest neighbours of @p query (dynamic, exact).
     *
     * Results are sorted ascending by distance.  If the index contains fewer
     * than @p k points, all are returned.
     */
    [[nodiscard]] std::vector<std::size_t> kNearest(const std::vector<float>& query,
                                                    std::size_t k) const;

    /**
     * Return the precomputed K nearest neighbours of stored point @p i (O(1)).
     *
     * Returns an empty span if @p i is out of range or `build()` has not been
     * called.  The returned span is valid for the lifetime of this index.
     */
    [[nodiscard]] std::span<const std::size_t> neighbors(std::size_t i) const;

private:
    static constexpr std::size_t kNull = std::numeric_limits<std::size_t>::max();

    struct Node {
        std::size_t vantage_idx = 0;  ///< Index into items_[].
        double threshold = 0.0;       ///< Median distance from vantage point.
        std::size_t left = kNull;     ///< Node index for d <= threshold.
        std::size_t right = kNull;    ///< Node index for d > threshold.
    };

    using Heap = std::priority_queue<std::pair<double, std::size_t>>;

    DistFn dist_;
    std::vector<std::vector<float>> items_;
    std::vector<std::size_t> indices_;  ///< Scratch reordered during build.
    std::vector<Node> nodes_;
    std::size_t root_ = kNull;
    std::size_t k_ = 0;

    std::vector<std::vector<std::size_t>> precomputed_;  ///< K-NN per point.

    std::size_t buildNode(std::size_t begin, std::size_t end);
    void searchNode(std::size_t node_idx, const std::vector<float>& query, std::size_t k,
                    Heap& heap, double& tau) const;
};

}  // namespace audio
