#pragma once

#include <string>
#include <variant>

namespace audio {

/**
 * Discrete lifecycle commands polled from IParamController.
 *
 * These are sent by a controller (UI, MIDI, OSC, etc.) to the processing
 * loop.  The Swift app can implement IParamController and enqueue these
 * to drive the audio engine.
 */

struct StartCommand {};

struct StopCommand {};

struct RecordCommand {
    std::string output_path;
};

struct RebuildCommand {
    int block_size = 4096;
    int overlap = 0;
};

using Command = std::variant<StartCommand, StopCommand, RecordCommand, RebuildCommand>;

}  // namespace audio
