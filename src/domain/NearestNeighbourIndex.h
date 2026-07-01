#pragma once

#include <cstddef>
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
 * ## Storage model
 * Fingerprints are stored in a single flat row-major matrix (N × dim `float`)
 * instead of N separate heap-allocated vectors.  This gives O(N) linear scans
 * a contiguous, cache-friendly memory layout and makes the distance inner loops
 * auto-vectorizable.
 *
 * ## Distance metric
 * The index always uses Euclidean distance over `float` vectors.
 * This is hardcoded to allow inlining and SIMD-friendly code generation.
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
 *   idx.build(mel_matrix, mel_dim, 32);    // K = 32 precomputed neighbours
 *   auto nn = idx.kNearest(target_mel, 8); // O(log N) dynamic query
 *   auto synapses = idx.neighbors(i);      // O(1) precomputed synapse list
 * @endcode
 *
 * ## Complexity
 * - Build: O(N log N) VP tree construction + O(N·K·log N) precomputation.
 * - kNearest: O(log N) average, O(N) worst case.
 * - neighbors: O(1).
 */
class NearestNeighbourIndex {
public:
    NearestNeighbourIndex() = default;

    NearestNeighbourIndex(const NearestNeighbourIndex&) = delete;
    NearestNeighbourIndex& operator=(const NearestNeighbourIndex&) = delete;
    NearestNeighbourIndex(NearestNeighbourIndex&&) = default;
    NearestNeighbourIndex& operator=(NearestNeighbourIndex&&) = default;

    /**
     * Build the VP tree and precompute K nearest neighbours for each point.
     *
     * @param flat_matrix  Row-major float matrix: N rows × @p dim cols (moved in).
     * @param dim          Number of floats per fingerprint row.
     * @param k            Number of nearest neighbours to precompute per point.
     *
     * Pre-condition: flat_matrix.size() == N × dim.
     */
    void build(std::vector<float> flat_matrix, std::size_t dim, std::size_t k);

    [[nodiscard]] bool empty() const { return n_items_ == 0; }
    [[nodiscard]] std::size_t size() const { return n_items_; }

    /**
     * Return up to @p k nearest neighbours of @p query (dynamic, exact).
     *
     * @param query  Must have exactly `dim()` elements.
     * Results are sorted ascending by distance.
     */
    [[nodiscard]] std::vector<std::size_t> kNearest(std::span<const float> query,
                                                    std::size_t k) const;

    /** Convenience overload accepting a `vector<float>`. */
    [[nodiscard]] std::vector<std::size_t> kNearest(const std::vector<float>& query,
                                                    std::size_t k) const {
        return kNearest(std::span<const float>(query), k);
    }

    /**
     * Return the precomputed K nearest neighbours of stored point @p i (O(1)).
     *
     * Returns an empty span if @p i is out of range.
     * Valid for the lifetime of this index.
     */
    [[nodiscard]] std::span<const std::size_t> neighbors(std::size_t i) const;

    /** Number of floats per fingerprint row. */
    [[nodiscard]] std::size_t dim() const noexcept { return dim_; }

    /** Raw pointer to the start of row @p i in the flat matrix. */
    [[nodiscard]] const float* row(std::size_t i) const noexcept {
        return items_.data() + i * dim_;
    }

private:
    static constexpr std::size_t kNull = std::numeric_limits<std::size_t>::max();

    struct Node {
        std::size_t vantage_idx = 0;
        double threshold = 0.0;
        std::size_t left = kNull;
        std::size_t right = kNull;
    };

    using Heap = std::priority_queue<std::pair<double, std::size_t>>;

    std::vector<float> items_;  ///< Flat row-major matrix: n_items_ × dim_.
    std::size_t n_items_ = 0;
    std::size_t dim_ = 0;
    std::vector<std::size_t> indices_;
    std::vector<Node> nodes_;
    std::size_t root_ = kNull;
    std::size_t k_ = 0;

    std::vector<std::vector<std::size_t>> precomputed_;

    std::size_t buildNode(std::size_t begin, std::size_t end);
    void searchNode(std::size_t node_idx, std::span<const float> query, std::size_t k, Heap& heap,
                    double& tau) const;

    /** Euclidean distance between row @p i and the given query. */
    [[nodiscard]] double distToRow(std::size_t i, std::span<const float> query) const noexcept;
};

}  // namespace audio
