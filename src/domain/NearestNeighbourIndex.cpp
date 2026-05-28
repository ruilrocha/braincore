#include "NearestNeighbourIndex.h"

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>
#include <vector>

namespace audio {

void NearestNeighbourIndex::build(std::vector<std::vector<double>> points, DistFn dist,
                                  std::size_t k) {
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

std::vector<std::size_t> NearestNeighbourIndex::kNearest(const std::vector<double>& query,
                                                         std::size_t k) const {
    if (items_.empty() || root_ == kNull) {
        return {};
    }
    k = std::min(k, items_.size());

    Heap heap;  // max-heap; keeps the k smallest
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

std::span<const std::size_t> NearestNeighbourIndex::neighbors(std::size_t i) const {
    if (i >= precomputed_.size()) {
        return {};
    }
    return precomputed_[i];
}

std::size_t NearestNeighbourIndex::buildNode(std::size_t begin, std::size_t end) {
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
    for (const auto& orig_i : dists | std::views::values) {
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

void NearestNeighbourIndex::searchNode(std::size_t node_idx, const std::vector<double>& query,
                                       std::size_t k, Heap& heap, double& tau) const {
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

}  // namespace audio
