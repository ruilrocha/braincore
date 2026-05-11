#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>

#include "../../domain/port/IParamController.h"

namespace audio::adapter::control {

/**
 * Real-time parameter controller via WebSocket.
 *
 * Runs an ixwebsocket WebSocket server on a configurable port (default 7770).
 * A companion HTML/JS control panel (web/control-panel.html) connects via
 * WebSocket and sends JSON messages to update SearchParams in real-time.
 *
 * JSON protocol (client → server):
 *   Parameter:  { "param": "<name>", "value": <number> }
 *   Command:    { "command": "<name>", ... }
 *
 * JSON protocol (server → client, on connect):
 *   { "type": "state", "params": {...}, "config": {...} }
 */
class WebSocketParamController final : public port::IParamController {
public:
    /**
     * @param port    TCP port to listen on (default 7770).
     * @param silent  If true, suppress the startup banner.
     */
    explicit WebSocketParamController(int port = 7770, bool silent = false);
    ~WebSocketParamController() override;

    // Non-copyable, non-movable.
    WebSocketParamController(const WebSocketParamController&) = delete;
    WebSocketParamController& operator=(const WebSocketParamController&) = delete;

    void start() override;
    void stop() override;

    [[nodiscard]] SearchParams getParams() const override;
    void setParams(const SearchParams& params) override;

    [[nodiscard]] std::optional<Command> pollCommand() override;
    void setConfigState(const ConfigState& config) override;

    /// Print available parameter names and their ranges to stdout.
    static void printParamInfo();

private:
    /// Apply a named parameter value to the stored params.
    /// Caller must hold params_mutex_.
    void applyParam(const std::string& name, double value);
    void handleMessage(const std::string& msg);
    void broadcastState() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;

    mutable std::mutex mutex_;
    SearchParams       params_;
    ConfigState        config_state_;
    std::queue<Command> command_queue_;

    int port_;
    bool silent_{false};
    std::atomic<bool> running_{false};
};

} // namespace audio::adapter::control

