#pragma once

#include <cstdint>
#include <string>
#include "config/ConfigStore.h"

namespace esp32fan {

enum class Mode { Manual, Auto };
enum class CommandSource { Button, Infrared, Web, System };
enum class SensorStatus { Initializing, Ok, Invalid, Stale };
enum class Fault { None, TemperatureUnavailable };
enum class CommandResult { Accepted, TemperatureUnavailable, InvalidCommand };

// Temperature fields remain unavailable until ESP-017/019 are integrated.
// The HTTP adapter translates hasTemperature/hasSample=false to JSON null.
struct DeviceState {
    std::string bootId;
    std::uint64_t revision = 0;
    Mode mode = Mode::Manual;
    bool fanOutputOn = false;
    bool hasTemperature = false;
    double temperatureC = 0;
    SensorStatus sensorStatus = SensorStatus::Initializing;
    bool hasSample = false;
    std::uint32_t sampleAgeMs = 0;
    double onThresholdC = 30;
    double offThresholdC = 28;
    Fault fault = Fault::None;
    CommandSource lastCommandSource = CommandSource::System;
    bool wifiConnected = false;
};

class FanController {
public:
    // App supplies a non-empty ID unique to this boot. No hardware access here.
    explicit FanController(std::string bootId, TemperatureConfig config = TemperatureConfig{});

    DeviceState snapshot() const;
    CommandResult setPower(bool on, CommandSource source);
    CommandResult setMode(Mode mode, CommandSource source);
    CommandResult toggleFromButton();
    void setWifiConnected(bool connected);
    SaveResult setConfig(ConfigStore& store, const TemperatureConfig& config);

private:
    void acceptManual(bool on, CommandSource source);
    DeviceState state_;
};

} // namespace esp32fan
