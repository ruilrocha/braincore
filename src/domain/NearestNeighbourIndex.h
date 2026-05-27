#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <limits>
#include <queue>
#include <span>
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
 *   SynapticSearch and MarkovChainSearch for O(1) neighbourhood access.
 *
 * ## Usage
 * @code
 *   NearestNeighbourIndex idx;
 *   idx.build(fingerprints, dist_fn, 32);          // K = 32 precomputed neighbours
 *   auto nn = idx.kNearest(target_fp, 8);          // O(log N) dynamic query
 *   auto synapses = idx.neighbors(block_i);         // O(1) precomputed synapse list
 * @endcode
 *
 * ## Complexity
 * - Build: O(N log N) VP tree construction + O(N·K·log N) precomputation.
 * - kNearest: O(log N) average, O(N) worst case.
 * - neighbors: O(1).
 */
class NearestNeighbourIndex {
public:
    using DistFn = std::function<double(const std::vector<double>&, const std::vector<double>&)>;

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
     *                Also the default for subsequent kNearest() calls.
     */
    void build(std::vector<std::vector<double>> points, DistFn dist, std::size_t k) {
        dist_ = std::move(dist);
        items_ = std::move(points);
        k_ = k;

        indices_.resize(items_.size());
        for (std::size_t i = 0; i < indices_.size(); ++i) {
            indices_[i] = i;
        }
        nodes_.clear();
        root_ = buildNode(0, indices_.size());

        // Precompute K nearest neighbours for each stored point (skip self).
        precomputed_.resize(items_.size());
        for (std::size_t i = 0; i < items_.size(); ++i) {
            auto candidates = kNearest(items_[i], k + 1);
            // Remove self-match (the point itself appears at distance ≈ 0).
            if (auto it = std::ranges::find(candidates, i); it != candidates.end()) {
                candidates.erase(it);
            }
            if (candidates.size() > k) {
                candidates.resize(k);
            }
            precomputed_[i] = std::move(candidates);
        }
    }

    [[nodiscard]] bool empty() const { return items_.empty(); }
    [[nodiscard]] std::size_t size() const { return items_.size(); }

    /**
     * Return up to @p k nearest neighbours of @p query (dynamic, exact).
     *
     * Results are sorted ascending by distance.  If the index contains fewer
     * than @p k points, all are returned.
     *
     * @param query  Fingerprint to search for.
     * @param k      Maximum number of neighbours.
     */
    [[nodiscard]] std::vector<std::size_t> kNearest(const std::vector<double>& query,
                                                    std::size_t k) const {
        if (items_.empty() || root_ == kNull) {
            return {};
        }
        k = std::min(k, items_.size());

        using Pair = std::pair<double, std::size_t>;
        std::priority_queue<Pair> heap;  // max-heap; keeps the k smallest
        double tau = std::numeric_limits<double>::max();

        searchNode(root_, query, k, heap, tau);

        std::vector<std::size_t> result;
        result.reserve(heap.size());
        while (!heap.empty()) {
            result.push_back(heap.top().second);
            heap.pop();
        }
        std::ranges::reverse(result);  // closest first
        return result;
    }

    /**
     * Return the precomputed K nearest neighbours of stored point @p i (O(1)).
     *
     * Returns an empty span if @p i is out of range or `build()` has not been
     * called.  The returned span is valid for the lifetime of this index.
     *
     * @param i  Index of the stored point (i.e. block index in the Brain).
     */
    [[nodiscard]] std::span<const std::size_t> neighbors(std::size_t i) const {
        if (i >= precomputed_.size()) {
            return {};
        }
        return precomputed_[i];
    }

private:
    static constexpr std::size_t kNull = std::numeric_limits<std::size_t>::max();

    struct Node {
        std::size_t vantage_idx = 0;  ///< Index into items_[].
        double threshold = 0.0;       ///< Median distance from vantage point.
        std::size_t left = kNull;     ///< Node index for d <= threshold.
        std::size_t right = kNull;    ///< Node index for d > threshold.
    };

    DistFn dist_;
    std::vector<std::vector<double>> items_;
    std::vector<std::size_t> indices_;  ///< Scratch reordered during build.
    std::vector<Node> nodes_;
    std::size_t root_ = kNull;
    std::size_t k_ = 0;

    std::vector<std::vector<std::size_t>> precomputed_;  ///< K-NN per point (built in build()).

    // ── VP tree build ─────────────────────────────────────────────────────

    std::size_t buildNode(std::size_t begin, std::size_t end) {
        if (begin >= end) {
            return kNull;
        }

        const std::size_t node_idx = nodes_.size();
        nodes_.emplace_back();

        const std::size_t vp = indices_[end - 1];
        nodes_[node_idx].vantage_idx = vp;

        if (begin + 1 == end) {
            nodes_[node_idx].threshold = 0.0;
            return node_idx;
        }

        const std::size_t n = end - begin - 1;
        std::vector<std::pair<double, std::size_t>> dists;
        dists.reserve(n);
        for (std::size_t i = begin; i < end - 1; ++i) {
            dists.emplace_back(dist_(items_[vp], items_[indices_[i]]), i);
        }

        const std::size_t median_pos = n / 2;
        std::ranges::nth_element(dists, dists.begin() + static_cast<std::ptrdiff_t>(median_pos),
                                 [](const auto& a, const auto& b) { return a.first < b.first; });

        nodes_[node_idx].threshold = dists[median_pos].first;

        std::vector<std::size_t> reordered;
        reordered.reserve(n);
        for (const auto& [d, orig_i] : dists) {
            reordered.push_back(indices_[orig_i]);
        }
        for (std::size_t i = 0; i < n; ++i) {
            indices_[begin + i] = reordered[i];
        }

        const std::size_t left_end = begin + median_pos;
        const std::size_t right_end = end - 1;

        // Note: nodes_ may reallocate — access by index, not pointer.
        const std::size_t left_child = buildNode(begin, left_end);
        nodes_[node_idx].left = left_child;
        const std::size_t right_child = buildNode(left_end, right_end);
        nodes_[node_idx].right = right_child;

        return node_idx;
    }

    // ── VP tree search ────────────────────────────────────────────────────

    using Heap = std::priority_queue<std::pair<double, std::size_t>>;

    void searchNode(std::size_t node_idx, const std::vector<double>& query, std::size_t k,
                    Heap& heap, double& tau) const {
        if (node_idx == kNull) {
            return;
        }

        const Node& node = nodes_[node_idx];
        const double d = dist_(query, items_[node.vantage_idx]);

        if (heap.size() < k) {
            heap.emplace(d, node.vantage_idx);
            if (heap.size() == k) {
                tau = heap.top().first;
            }
        } else if (d < tau) {
            heap.pop();
            heap.emplace(d, node.vantage_idx);
            tau = heap.top().first;
        }

        if (node.left == kNull && node.right == kNull) {
            return;
        }

        if (d <= node.threshold) {
            searchNode(node.left, query, k, heap, tau);
            if (d + tau >= node.threshold) {
                searchNode(node.right, query, k, heap, tau);
            }
        } else {
            searchNode(node.right, query, k, heap, tau);
            if (d - tau <= node.threshold) {
                searchNode(node.left, query, k, heap, tau);
            }
        }
    }
};

}  // namespace audio
