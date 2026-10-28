#pragma once

#include <cstdint>

namespace esp32fan {

enum class TemperaturePoll { Pending, Ready, Invalid };

class TemperaturePort {
public:
    virtual ~TemperaturePort() = default;
    // All methods must return promptly; no wait-for-conversion or sleep here.
    // start() clears old results; false means no operation remains active.
    virtual bool start() = 0;
    // Ready is a new result for the current conversion, validated by the driver.
    // Ready/Invalid finish the operation; Pending never supplies a valid value.
    virtual TemperaturePoll poll(double& temperatureC) = 0;
    // Must discard late results before the next start, including buffered data.
    virtual void cancel() = 0;
};

struct TemperatureTiming {
    std::uint32_t intervalMs = 1000;
    std::uint32_t timeoutMs = 1000;
};

enum class TemperatureEventKind { None, Sample, Invalid };
enum class TemperatureDiagnostic { None, StartFailed, ReadFailed, NonFinite, Timeout, InvalidTiming };

struct TemperatureEvent {
    TemperatureEventKind kind = TemperatureEventKind::None;
    TemperatureDiagnostic diagnostic = TemperatureDiagnostic::None;
    double temperatureC = 0;
};

class TemperatureService {
public:
    explicit TemperatureService(TemperaturePort& port, TemperatureTiming timing = TemperatureTiming{});
    // One bounded step per main-loop iteration; time is monotonic uint32 ms.
    TemperatureEvent update(std::uint32_t nowMs);

private:
    TemperaturePort& port_;
    TemperatureTiming timing_;
    bool timingValid_;
    bool invalidTimingReported_ = false;
    bool started_ = false;
    bool pending_ = false;
    std::uint32_t startedAtMs_ = 0;
};

} // namespace esp32fan
