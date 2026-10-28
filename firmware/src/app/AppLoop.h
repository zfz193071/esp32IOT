#pragma once

#include "adapters/WriteApi.h"
#include "input/ButtonDebouncer.h"
#include "input/InfraredInput.h"
#include "services/NetworkService.h"
#include "services/TemperatureService.h"

namespace esp32fan {
class OutputPort {
public:
    virtual ~OutputPort() = default;
    // Board adapter must establish OFF before configuring other hardware.
    virtual void initializeOff() = 0;
    virtual void setFan(bool on) = 0;
    virtual void setIndicator(bool on) = 0;
};
class ApiPort {
public:
    virtual ~ApiPort() = default;
    // Nonblocking and bounded; returns at most one complete API request.
    // Static resources are handled separately by the board HTTP adapter.
    virtual bool takeRequest(WriteRequest& request) = 0;
    // Queue a response without waiting for the client to read it.
    virtual void sendResponse(const StateResponse& response) = 0;
};
class DiagnosticPort {
public:
    virtual ~DiagnosticPort() = default;
    virtual void startup(LoadResult result, const DeviceState& state) = 0;
    virtual void stateChanged(const DeviceState& state) = 0;
    virtual void temperature(TemperatureDiagnostic diagnostic) = 0;
    virtual void network(const NetworkUpdate& update) = 0;
};

class AppLoop {
public:
    AppLoop(std::string bootId, ConfigStore& store, OutputPort& output, DiagnosticPort& diagnostics,
            std::uint32_t debounceMs, TemperatureService* temperature = nullptr,
            NetworkService* network = nullptr, ApiPort* api = nullptr,
            WritePolicy trustedPolicy = WritePolicy{}, InfraredInput* infrared = nullptr);
    // Idempotent; storage load occurs only after hardware OFF initialization.
    void start();
    // Caller supplies normalized button level and one monotonic clock sample.
    // Temperature -> health -> network -> button -> one IR frame -> one API -> final outputs.
    bool step(std::uint32_t nowMs, bool buttonPressed);
    DeviceState snapshot() const;
private:
    ConfigStore& store_;
    OutputPort& output_;
    DiagnosticPort& diagnostics_;
    FanController controller_;
    ButtonDebouncer button_;
    TemperatureService* temperature_;
    NetworkService* network_;
    ApiPort* api_;
    InfraredInput* infrared_;
    WritePolicy trustedPolicy_;
    WritePolicy activePolicy_;
    bool started_ = false;
    bool outputOn_ = false;
    DeviceState lastLogged_;
};
} // namespace esp32fan
