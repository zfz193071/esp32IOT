#include "FanController.h"

#include <utility>

namespace esp32fan {
namespace {
bool validSource(CommandSource source) {
    switch (source) {
    case CommandSource::Button:
    case CommandSource::Infrared:
    case CommandSource::Web:
    case CommandSource::System:
        return true;
    }
    return false;
}
} // namespace

FanController::FanController(std::string bootId, TemperatureConfig config) {
    state_.bootId = std::move(bootId);
    if (validConfig(config)) {
        state_.onThresholdC = config.onThresholdC;
        state_.offThresholdC = config.offThresholdC;
    }
}

DeviceState FanController::snapshot() const {
    return state_;
}

void FanController::acceptManual(bool on, CommandSource source) {
    const bool changed = state_.mode != Mode::Manual ||
                         state_.fanOutputOn != on ||
                         state_.lastCommandSource != source;
    state_.mode = Mode::Manual;
    state_.fanOutputOn = on;
    state_.lastCommandSource = source;
    if (changed) {
        ++state_.revision;
    }
}

CommandResult FanController::setPower(bool on, CommandSource source) {
    if (!validSource(source)) {
        return CommandResult::InvalidCommand;
    }
    acceptManual(on, source);
    return CommandResult::Accepted;
}

CommandResult FanController::setMode(Mode mode, CommandSource source) {
    if (!validSource(source)) {
        return CommandResult::InvalidCommand;
    }
    switch (mode) {
    case Mode::Manual:
        acceptManual(state_.fanOutputOn, source);
        return CommandResult::Accepted;
    case Mode::Auto:
        return CommandResult::TemperatureUnavailable;
    }
    return CommandResult::InvalidCommand;
}

CommandResult FanController::toggleFromButton() {
    return setPower(!state_.fanOutputOn, CommandSource::Button);
}

void FanController::setWifiConnected(bool connected) {
    if (state_.wifiConnected != connected) {
        state_.wifiConnected = connected;
        ++state_.revision;
    }
}

SaveResult FanController::setConfig(ConfigStore& store, const TemperatureConfig& config) {
    const auto result = store.save(config);
    if (result == SaveResult::Saved || result == SaveResult::Unchanged) {
        if (state_.onThresholdC != config.onThresholdC || state_.offThresholdC != config.offThresholdC) {
            state_.onThresholdC = config.onThresholdC;
            state_.offThresholdC = config.offThresholdC;
            ++state_.revision;
        }
    }
    return result;
}

} // namespace esp32fan
