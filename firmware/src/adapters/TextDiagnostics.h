#pragma once

#include "app/AppLoop.h"

namespace esp32fan {
class DiagnosticSink {
public:
    virtual ~DiagnosticSink() = default;
    // Accept one complete line (without newline) or return false immediately.
    // A board sink must use bounded buffering, never wait for a serial reader.
    virtual bool tryWriteLine(const std::string& line) = 0;
};

class TextDiagnostics : public DiagnosticPort {
public:
    explicit TextDiagnostics(DiagnosticSink& sink) : sink_(sink) {}
    void startup(LoadResult result, const DeviceState& state) override;
    void stateChanged(const DeviceState& state) override;
    void temperature(TemperatureDiagnostic diagnostic) override;
    void network(const NetworkUpdate& update) override;
    std::uint32_t droppedLines() const { return dropped_; }
private:
    void emit(const std::string& line);
    DiagnosticSink& sink_;
    std::uint32_t dropped_ = 0;
};
} // namespace esp32fan
