#include "WebSocketParamController.h"

#include <format>
#include <iostream>
#include <sstream>
#include <string>

#include <ixwebsocket/IXWebSocketServer.h>

namespace audio::adapter::control {

// ── Minimal JSON helpers (no external JSON library) ────────────────────

/// Extract the string value of a JSON key like "key":"value".
static bool extractString(const std::string& json, const std::string& key,
                           std::string& out) {
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return false;
    auto colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return false;
    auto q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return false;
    auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return false;
    out = json.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

/// Extract the numeric value of a JSON key like "key":123.4.
static bool extractNumber(const std::string& json, const std::string& key,
                            double& out) {
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return false;
    auto colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return false;
    auto vstart = colon + 1;
    while (vstart < json.size() && (json[vstart] == ' ' || json[vstart] == '\t'))
        ++vstart;
    try {
        std::size_t end_pos = 0;
        out = std::stod(json.substr(vstart), &end_pos);
        return end_pos > 0;
    } catch (...) { return false; }
}

/// Extract a boolean value of a JSON key like "key":true.
static bool extractBool(const std::string& json, const std::string& key,
                          bool& out) {
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return false;
    auto colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return false;
    auto vstart = colon + 1;
    while (vstart < json.size() && (json[vstart] == ' ' || json[vstart] == '\t'))
        ++vstart;
    if (json.substr(vstart, 4) == "true")  { out = true;  return true; }
    if (json.substr(vstart, 5) == "false") { out = false; return true; }
    return false;
}

/// Serialize params + config to a full state JSON string.
static std::string buildStateJson(const SearchParams& p,
                                   const port::IParamController::ConfigState& c) {
    std::ostringstream ss;
    ss << R"({"type":"state","params":{)";
    ss << std::format(
        R"("alpha":{},"stickyness":{},"overlap":{},"usage_falloff":{},"usage_weight":{},)"
        R"("blend_ratio":{},"n_ratio":{},"secondary_start":{},"secondary_end":{},)"
        R"("momentum":{},"momentum_decay":{},)"
        R"("grain_size":{},"grain_scatter":{},"grain_density":{},)"
        R"("grain_size_variation":{},"grain_amp_variation":{},)"
        R"("grain_pitch_jitter":{},"grain_hop_randomness":{},)"
        R"("spectral_morph":{},)"
        R"("stutter_chance":{},"stutter_count":{},)"
        R"("envelope_shape":{},"envelope_amount":{})",
        p.alpha, p.stickyness, p.overlap, p.usage_falloff, p.usage_weight,
        p.blend_ratio, p.n_ratio, p.secondary_start, p.secondary_end,
        p.momentum, p.momentum_decay,
        p.grain_size, p.grain_scatter, p.grain_density,
        p.grain_size_variation, p.grain_amp_variation,
        p.grain_pitch_jitter, p.grain_hop_randomness,
        p.spectral_morph,
        p.stutter_chance, p.stutter_count,
        p.envelope_shape, p.envelope_amount);
    ss << "}";
    ss << std::format(
        R"(,"config":{{"block_size":{},"overlap":{},"window_shape":{},"search_strategy":"{}","num_synapses":{},"target_path":"{}","playing":{},"recording":{}}})",
        c.block_size, c.overlap, c.window_shape, c.search_strategy,
        c.num_synapses, c.target_path,
        c.playing ? "true" : "false", c.recording ? "true" : "false");
    ss << "}";
    return ss.str();
}

// ── Implementation ─────────────────────────────────────────────────────

struct WebSocketParamController::Impl {
    ix::WebSocketServer server;
    explicit Impl(int port)
        : server(port, "0.0.0.0") {}
};

WebSocketParamController::WebSocketParamController(const int port)
    : port_(port) {}

WebSocketParamController::~WebSocketParamController() {
    stop();
}

void WebSocketParamController::handleMessage(const std::string& msg) {
    // Try command first: {"command":"start"} or {"command":"rebuild","block_size":2048,...}
    std::string command_name;
    if (extractString(msg, "command", command_name)) {
        std::lock_guard lock(mutex_);
        if (command_name == "start") {
            StartCommand cmd;
            extractString(msg, "target_path", cmd.target_path);
            command_queue_.push(std::move(cmd));
        } else if (command_name == "stop") {
            command_queue_.push(StopCommand{});
        } else if (command_name == "record") {
            RecordCommand cmd;
            extractBool(msg, "enable", cmd.enable);
            extractString(msg, "path", cmd.path);
            command_queue_.push(std::move(cmd));
        } else if (command_name == "rebuild") {
            RebuildCommand cmd;
            double v = 0;
            if (extractNumber(msg, "block_size", v))      cmd.block_size = static_cast<int>(v);
            if (extractNumber(msg, "overlap", v))          cmd.overlap = static_cast<int>(v);
            if (extractNumber(msg, "window_shape", v))     cmd.window_shape = static_cast<int>(v);
            if (extractNumber(msg, "num_synapses", v))     cmd.num_synapses = static_cast<int>(v);
            extractString(msg, "search_strategy", cmd.search_strategy);
            command_queue_.push(std::move(cmd));
        }
        return;
    }

    // Try parameter update: {"param":"alpha","value":0.5}
    std::string param_name;
    double value = 0.0;
    if (extractString(msg, "param", param_name) && extractNumber(msg, "value", value)) {
        std::lock_guard lock(mutex_);
        applyParam(param_name, value);
    }
}

void WebSocketParamController::broadcastState() const {
    // mutex_ must be held by caller or called when safe.
    if (!impl_) return;
    const std::string json = buildStateJson(params_, config_state_);
    for (auto& client : impl_->server.getClients()) {
        client->send(json);
    }
}

void WebSocketParamController::start() {
    if (running_) return;

    impl_ = std::make_unique<Impl>(port_);

    impl_->server.setOnClientMessageCallback(
        [this](std::shared_ptr<ix::ConnectionState> /*state*/,
               ix::WebSocket& ws,
               const ix::WebSocketMessagePtr& msg) {
            if (msg->type == ix::WebSocketMessageType::Open) {
                std::lock_guard lock(mutex_);
                ws.send(buildStateJson(params_, config_state_));
            }
            else if (msg->type == ix::WebSocketMessageType::Message) {
                handleMessage(msg->str);
            }
        });

    auto res = impl_->server.listen();
    if (!res.first) {
        std::cerr << std::format("WebSocket server failed to listen on port {}: {}\n",
                                  port_, res.second);
        return;
    }

    impl_->server.start();
    running_ = true;

    std::cout << std::format(
        "\n╔══════════════════════════════════════════════════╗\n"
        "║  WebSocket control server: ws://localhost:{:<5}  ║\n"
        "║  Open web/control-panel.html in your browser     ║\n"
        "╚══════════════════════════════════════════════════╝\n\n",
        port_);
}

void WebSocketParamController::stop() {
    if (!running_) return;
    running_ = false;

    if (impl_) {
        impl_->server.stop();
        impl_.reset();
    }
}

SearchParams WebSocketParamController::getParams() const {
    std::lock_guard lock(mutex_);
    return params_;
}

void WebSocketParamController::setParams(const SearchParams& params) {
    std::lock_guard lock(mutex_);
    params_ = params;
}

std::optional<Command> WebSocketParamController::pollCommand() {
    std::lock_guard lock(mutex_);
    if (command_queue_.empty()) return std::nullopt;
    auto cmd = std::move(command_queue_.front());
    command_queue_.pop();
    return cmd;
}

void WebSocketParamController::setConfigState(const ConfigState& config) {
    std::lock_guard lock(mutex_);
    config_state_ = config;
    broadcastState();
}

void WebSocketParamController::applyParam(const std::string& name, double value) {
    if      (name == "alpha")                params_.alpha = value;
    else if (name == "stickyness")           params_.stickyness = value;
    else if (name == "overlap")              params_.overlap = static_cast<int>(value);
    else if (name == "usage_falloff")        params_.usage_falloff = value;
    else if (name == "usage_weight")         params_.usage_weight = value;
    else if (name == "blend_ratio")          params_.blend_ratio = value;
    else if (name == "n_ratio")              params_.n_ratio = value;
    else if (name == "secondary_start")      params_.secondary_start = static_cast<int>(value);
    else if (name == "secondary_end")        params_.secondary_end = static_cast<int>(value);
    else if (name == "momentum")             params_.momentum = value;
    else if (name == "momentum_decay")       params_.momentum_decay = value;
    else if (name == "grain_size")           params_.grain_size = value;
    else if (name == "grain_scatter")        params_.grain_scatter = value;
    else if (name == "grain_density")        params_.grain_density = value;
    else if (name == "grain_size_variation") params_.grain_size_variation = value;
    else if (name == "grain_amp_variation")  params_.grain_amp_variation = value;
    else if (name == "grain_pitch_jitter")   params_.grain_pitch_jitter = value;
    else if (name == "grain_hop_randomness") params_.grain_hop_randomness = value;
    else if (name == "spectral_morph")       params_.spectral_morph = value;
    else if (name == "stutter_chance")       params_.stutter_chance = value;
    else if (name == "stutter_count")        params_.stutter_count = static_cast<int>(value);
    else if (name == "envelope_shape")       params_.envelope_shape = static_cast<int>(value);
    else if (name == "envelope_amount")      params_.envelope_amount = value;
}

void WebSocketParamController::printParamInfo() {
    std::cout <<
        "Available parameters:\n"
        "  alpha               [0.0, 1.0]   Source-vs-target blend\n"
        "  stickyness          [0.0, 1.0]   Temporal coherence bias\n"
        "  usage_falloff       [0.0, 1.0]   Boredom: usage decay rate\n"
        "  usage_weight        [0.0, 1.0]   Novelty: usage penalty\n"
        "  blend_ratio         [0.0, 1.0]   Primary/secondary FP blend\n"
        "  n_ratio             [0.0, 1.0]   Raw/normalised FP blend\n"
        "  momentum            [0.0, 1.0]   Trajectory inertia\n"
        "  momentum_decay      [0.0, 1.0]   Velocity decay per step\n"
        "  grain_size          [0.01, 1.0]  Grain size (frac of block)\n"
        "  grain_scatter       [0.0, 1.0]   Grain temporal scatter\n"
        "  grain_density       [0.1, 4.0]   Grain overlap density\n"
        "  spectral_morph      [0.0, 1.0]   Spectral morph amount\n"
        "  stutter_chance      [0.0, 1.0]   Stutter probability\n"
        "  stutter_count       [2, 8]       Stutter repetitions\n"
        "  envelope_shape      [0, 4]       Envelope type\n"
        "  envelope_amount     [0.0, 1.0]   Envelope intensity\n";
}

} // namespace audio::adapter::control

