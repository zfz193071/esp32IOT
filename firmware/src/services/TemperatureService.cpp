#include "TemperatureService.h"

#include <cmath>

namespace esp32fan {
namespace {
TemperatureEvent invalid(TemperatureDiagnostic reason) {
    TemperatureEvent event;
    event.kind = TemperatureEventKind::Invalid;
    event.diagnostic = reason;
    return event;
}
} // namespace

TemperatureService::TemperatureService(TemperaturePort& port, TemperatureTiming timing)
    : port_(port), timing_(timing),
      timingValid_(timing.intervalMs > 0 && timing.timeoutMs > 0 &&
                   timing.intervalMs < 0x80000000u && timing.timeoutMs < 0x80000000u) {}

TemperatureEvent TemperatureService::update(std::uint32_t nowMs) {
    if (!timingValid_) {
        if (!invalidTimingReported_) {
            invalidTimingReported_ = true;
            return invalid(TemperatureDiagnostic::InvalidTiming);
        }
        return {};
    }
    if (pending_) {
        // Deadline wins even if the port has a late ready value buffered.
        if (static_cast<std::uint32_t>(nowMs - startedAtMs_) >= timing_.timeoutMs) {
            pending_ = false;
            port_.cancel();
            return invalid(TemperatureDiagnostic::Timeout);
        }
        double value = 0;
        const auto result = port_.poll(value);
        if (result == TemperaturePoll::Pending) return {};
        pending_ = false;
        if (result != TemperaturePoll::Ready) return invalid(TemperatureDiagnostic::ReadFailed);
        if (!std::isfinite(value)) return invalid(TemperatureDiagnostic::NonFinite);
        TemperatureEvent event;
        event.kind = TemperatureEventKind::Sample;
        event.temperatureC = value;
        return event;
    }
    if (started_ && static_cast<std::uint32_t>(nowMs - startedAtMs_) < timing_.intervalMs) return {};
    // Schedule from this attempt, not from a catch-up loop after a long delay.
    started_ = true;
    startedAtMs_ = nowMs;
    if (!port_.start()) return invalid(TemperatureDiagnostic::StartFailed);
    pending_ = true;
    return {};
}

} // namespace esp32fan
