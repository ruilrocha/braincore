#pragma once

#include <string>
#include <variant>

namespace audio {

/**
 * Commands that can be sent from a UI/controller to the main event loop.
 * These represent discrete lifecycle actions, not continuous parameter tweaks.
 */

/// Start playback. If target_path is set, uses stream mode; otherwise infinite.
struct StartCommand {
    std::string target_path;  ///< Empty = infinite mode, non-empty = stream mode.
};

/// Stop playback.
struct StopCommand {};

/// Toggle recording on/off.
struct RecordCommand {
    bool enable = true;
    std::string path;
};

/// Rebuild the brain with a new configuration.
struct RebuildCommand {
    int block_size = 4096;
    int overlap = 0;
    int window_shape = 0;         ///< Maps to WindowShape enum ordinal.
    std::string search_strategy;  ///< "closest","reverse","synaptic", etc.
    int num_synapses = 1000;
};

/// A tagged union of all possible commands.
using Command = std::variant<StartCommand, StopCommand, RecordCommand, RebuildCommand>;

}  // namespace audio
