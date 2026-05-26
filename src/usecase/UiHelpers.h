#pragma once

#include "../domain/BlockConfig.h"
#include "../domain/port/ISearchStrategy.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>

// Adapter includes needed for the factory — only used by the Composition Root.
#include "../adapter/search/ClosestSearch.h"
#include "../adapter/search/MarkovChainSearch.h"
#include "../adapter/search/MomentumSearch.h"
#include "../adapter/search/SynapticSearch.h"
#include "../adapter/search/VpTreeSearch.h"

namespace audio::ui {

// ── Path resolution ────────────────────────────────────────────────────

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

inline std::string resolvePath(const std::string& relative) {
    return (std::filesystem::path(PROJECT_ROOT) / relative).string();
}

// ── Audio file detection ───────────────────────────────────────────────

inline bool isAudioFile(const std::filesystem::path& path) {
    static constexpr std::array extensions = {
        ".wav", ".flac", ".ogg", ".aif", ".aiff", ".w64", ".rf64", ".raw", ".caf", ".mp3",
    };
    auto ext = path.extension().string();
    for (auto& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const auto* e : extensions) {
        if (ext == e) {
            return true;
        }
    }
    return false;
}

// ── Search strategy factory ────────────────────────────────────────────

/**
 * Create a search strategy by name.
 *
 * All strategies receive the Brain at search time via ISearchStrategy::search().
 * Strategies that require the nearest-neighbour index (synaptic, markov,
 * vptree) throw at search time if brain->buildIndex() has not been called —
 * there is no constructor-time injection.
 *
 * @param name  Strategy name: "synaptic", "markov", "momentum", or anything
 *              else defaults to ClosestSearch.
 */
inline auto makeSearch(const std::string& name) -> std::shared_ptr<port::ISearchStrategy> {
    using namespace adapter::search;
    if (name == "synaptic") {
        return std::make_shared<SynapticSearch>(100);
    }
    if (name == "markov") {
        return std::make_shared<MarkovChainSearch>(1.0, 100);
    }
    if (name == "vptree") {
        return std::make_shared<VpTreeSearch>(32);
    }
    if (name == "momentum") {
        return std::make_shared<MomentumSearch>();
    }
    return std::make_shared<ClosestSearch>();
}

// ── WindowShape from ordinal ───────────────────────────────────────────

inline WindowShape windowFromOrdinal(int ordinal) {
    switch (ordinal) {
        case 0:
            return WindowShape::Rectangle;
        case 1:
            return WindowShape::Hamming;
        case 2:
            return WindowShape::Hann;
        case 3:
            return WindowShape::Blackman;
        case 4:
            return WindowShape::Bartlett;
        case 5:
            return WindowShape::FlatTop;
        default:
            return WindowShape::Gaussian;
    }
}

}  // namespace audio::ui
