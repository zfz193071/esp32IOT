#include "FanController.h"

#include <utility>
#include <cmath>

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
        if (state_.sensorStatus != SensorStatus::Ok || !state_.hasTemperature ||
            !state_.hasSample || state_.sampleAgeMs > 5000) {
            return CommandResult::TemperatureUnavailable;
        }
        {
            const auto before = state_;
            if (state_.mode != Mode::Auto) state_.fanOutputOn = false;
            state_.mode = Mode::Auto;
            applyAutomatic();
            state_.lastCommandSource = source;
            if (before.mode != state_.mode || before.fanOutputOn != state_.fanOutputOn ||
                before.lastCommandSource != state_.lastCommandSource) ++state_.revision;
        }
        return CommandResult::Accepted;
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
            applyAutomatic();
            ++state_.revision;
        }
    }
    return result;
}

void FanController::applyAutomatic() {
    if (state_.mode != Mode::Auto) return;
    if (state_.sensorStatus != SensorStatus::Ok || !state_.hasTemperature) {
        state_.fanOutputOn = false;
        state_.mode = Mode::Manual;
        state_.fault = Fault::TemperatureUnavailable;
        state_.lastCommandSource = CommandSource::System;
        return;
    }
    const bool previous = state_.fanOutputOn;
    if (state_.temperatureC >= state_.onThresholdC) state_.fanOutputOn = true;
    else if (state_.temperatureC <= state_.offThresholdC) state_.fanOutputOn = false;
    if (state_.fanOutputOn != previous) state_.lastCommandSource = CommandSource::System;
}

void FanController::finishTemperatureUpdate(const DeviceState& before) {
    if (state_.sensorStatus == SensorStatus::Ok) state_.fault = Fault::None;
    applyAutomatic();
    // Age progresses without changing the business revision.
    if (before.sensorStatus != state_.sensorStatus || before.hasSample != state_.hasSample ||
        before.hasTemperature != state_.hasTemperature || before.temperatureC != state_.temperatureC ||
        before.mode != state_.mode || before.fanOutputOn != state_.fanOutputOn ||
        before.fault != state_.fault || before.lastCommandSource != state_.lastCommandSource) {
        ++state_.revision;
    }
}

void FanController::recordTemperature(double temperatureC, std::uint32_t nowMs) {
    if (!std::isfinite(temperatureC)) {
        invalidateTemperature(nowMs);
        return;
    }
    const auto before = state_;
    state_.temperatureC = temperatureC;
    state_.hasTemperature = true;
    state_.hasSample = true;
    state_.sampleAgeMs = 0;
    state_.sensorStatus = SensorStatus::Ok;
    lastSampleMs_ = nowMs;
    finishTemperatureUpdate(before);
}

void FanController::invalidateTemperature(std::uint32_t nowMs) {
    const auto before = state_;
    state_.hasTemperature = false;
    state_.temperatureC = 0;
    state_.sensorStatus = SensorStatus::Invalid;
    if (state_.hasSample) state_.sampleAgeMs = nowMs - lastSampleMs_;
    finishTemperatureUpdate(before);
}

void FanController::tick(std::uint32_t nowMs) {
    if (!state_.hasSample) return;
    const auto before = state_;
    state_.sampleAgeMs = nowMs - lastSampleMs_;
    if (state_.sensorStatus == SensorStatus::Ok && state_.sampleAgeMs > 5000) {
        state_.sensorStatus = SensorStatus::Stale;
        state_.hasTemperature = false;
        state_.temperatureC = 0;
    }
    finishTemperatureUpdate(before);
}

} // namespace esp32fan
