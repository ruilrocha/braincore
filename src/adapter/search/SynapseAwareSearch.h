#pragma once

#include "../../domain/SynapseGraph.h"
#include "../../domain/port/ISearchStrategy.h"

#include <memory>
#include <stdexcept>

namespace audio::adapter::search {

/**
 * Abstract base for search strategies that require a pre-computed SynapseGraph.
 *
 * The graph is injected at construction time — if null is passed the constructor
 * throws `std::invalid_argument` immediately, so any derived instance is always
 * in a valid, ready-to-use state.  There is no post-construction setter and no
 * silent fallback to a different algorithm.
 *
 * ## Extending
 * Derive, pass the graph to this constructor, and implement `search()`.
 * `graph_` is guaranteed non-null for the lifetime of the object.
 */
class SynapseAwareSearch : public port::ISearchStrategy {
protected:
    explicit SynapseAwareSearch(std::shared_ptr<const SynapseGraph> graph)
        : graph_(std::move(graph)) {
        if (!graph_) {
            throw std::invalid_argument(
                "SynapseAwareSearch: a non-null SynapseGraph is required at construction");
        }
    }

    std::shared_ptr<const SynapseGraph> graph_;
};

}  // namespace audio::adapter::search
