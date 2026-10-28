#include "AppLoop.h"
#include <utility>

namespace esp32fan {
AppLoop::AppLoop(std::string bootId, ConfigStore& store, OutputPort& output, DiagnosticPort& diagnostics,
                 std::uint32_t debounceMs, TemperatureService* temperature,
                 NetworkService* network, ApiPort* api, WritePolicy trustedPolicy)
    : store_(store), output_(output), diagnostics_(diagnostics), controller_(std::move(bootId)),
      button_(debounceMs), temperature_(temperature), network_(network), api_(api),
      trustedPolicy_(std::move(trustedPolicy)), activePolicy_(trustedPolicy_) {}

void AppLoop::start() {
    if (started_) return;
    output_.initializeOff();
    output_.setIndicator(false);
    const auto loaded = store_.load();
    controller_ = FanController(controller_.snapshot().bootId, store_.current());
    outputOn_ = false;
    lastLogged_ = controller_.snapshot();
    started_ = true;
    diagnostics_.startup(loaded, lastLogged_);
}

bool AppLoop::step(std::uint32_t nowMs, bool buttonPressed) {
    if (!started_) return false;
    if (temperature_) {
        const auto event = temperature_->update(nowMs);
        if (event.kind == TemperatureEventKind::Sample) controller_.recordTemperature(event.temperatureC, nowMs);
        else if (event.kind == TemperatureEventKind::Invalid) controller_.invalidateTemperature(nowMs);
        if (event.diagnostic != TemperatureDiagnostic::None) diagnostics_.temperature(event.diagnostic);
    }
    controller_.tick(nowMs);
    if (network_) {
        const auto update = network_->update(nowMs);
        controller_.setWifiConnected(update.connected);
        // Rebuild from trusted static names, so an obsolete dynamic IP is removed.
        activePolicy_ = trustedPolicy_;
        if (update.connected && !update.address.empty()) activePolicy_.allowedOrigins.push_back("http://" + update.address);
        if (update.diagnostic != NetworkDiagnostic::None) diagnostics_.network(update);
    }
    if (button_.update(buttonPressed, nowMs)) controller_.toggleFromButton();

    WriteRequest request;
    StateResponse response;
    const bool hasRequest = api_ && api_->takeRequest(request);
    if (hasRequest) {
        if (request.path == "/api/v1/state") response = handleStateRequest(request.method, request.path, controller_.snapshot());
        else response = handleWriteRequest(request, activePolicy_, controller_, store_);
    }
    const auto state = controller_.snapshot();
    if (state.fanOutputOn != outputOn_) {
        output_.setFan(state.fanOutputOn);
        output_.setIndicator(state.fanOutputOn);
        outputOn_ = state.fanOutputOn;
    }
    if (state.mode != lastLogged_.mode || state.fanOutputOn != lastLogged_.fanOutputOn ||
        state.fault != lastLogged_.fault || state.lastCommandSource != lastLogged_.lastCommandSource ||
        state.onThresholdC != lastLogged_.onThresholdC || state.offThresholdC != lastLogged_.offThresholdC) {
        diagnostics_.stateChanged(state);
        lastLogged_ = state;
    }
    if (hasRequest) api_->sendResponse(response);
    return true;
}
DeviceState AppLoop::snapshot() const { return controller_.snapshot(); }
} // namespace esp32fan
