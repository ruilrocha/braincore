#pragma once

#include <optional>

#include "../Command.h"
#include "../SearchParams.h"

namespace audio::port {

/**
 * Port: real-time parameter controller.
 *
 * Abstracts the mechanism for receiving live parameter updates and
 * lifecycle commands so the use-case / main loop can react without
 * knowing about any specific protocol (WebSocket, MIDI, GUI, etc.).
 *
 * Two message types flow through this port:
 *   - **Parameters** (continuous): SearchParams snapshot, polled per block.
 *   - **Commands** (discrete): start, stop, record, rebuild — polled by
 *     the main event loop.
 *
 * Concrete implementations live in src/adapter/control/.
 */
class IParamController {
public:
    virtual ~IParamController() = default;

    /**
     * Start listening for parameter updates.
     * Called once before the streaming loop begins.
     */
    virtual void start() = 0;

    /**
     * Stop listening for parameter updates.
     * Called after the streaming loop ends.
     */
    virtual void stop() = 0;

    /**
     * Get a thread-safe snapshot of the current parameters.
     * May be called from the audio processing thread every block.
     */
    [[nodiscard]] virtual SearchParams getParams() const = 0;

    /**
     * Set the current parameters (thread-safe).
     * Called to initialize or bulk-update parameters.
     */
    virtual void setParams(const SearchParams& params) = 0;

    /**
     * Poll for the next pending command (thread-safe).
     * Returns std::nullopt if no command is queued.
     * Commands are consumed FIFO — each call removes one from the queue.
     */
    [[nodiscard]] virtual std::optional<Command> pollCommand() = 0;

    /**
     * Update the config state reported to connected clients.
     * Called by main after handling a rebuild or state change.
     */
    struct ConfigState {
        int         block_size      = 4096;
        int         overlap         = 0;
        int         window_shape    = 6; // Gaussian
        std::string search_strategy = "closest";
        int         num_synapses    = 1000;
        std::string target_path;         ///< Empty = infinite mode, set = stream mode.
        bool        playing         = false;
        bool        recording       = false;
    };

    virtual void setConfigState(const ConfigState& config) = 0;
};

} // namespace audio::port

