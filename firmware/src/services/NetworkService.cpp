#include "NetworkService.h"

namespace esp32fan {
NetworkService::NetworkService(NetworkPort& port, NetworkTiming timing)
    : port_(port), timing_(timing), valid_(timing.connectTimeoutMs > 0 && timing.retryDelayMs > 0 &&
        timing.connectTimeoutMs < 0x80000000u && timing.retryDelayMs < 0x80000000u) {}

NetworkUpdate NetworkService::fail(std::uint32_t nowMs, NetworkDiagnostic diagnostic) {
    port_.disconnect();
    attempting_ = false;
    waiting_ = true;
    failedAt_ = nowMs;
    current_.connected = false;
    current_.address.clear();
    current_.diagnostic = diagnostic;
    return current_;
}

NetworkUpdate NetworkService::update(std::uint32_t nowMs) {
    current_.diagnostic = NetworkDiagnostic::None;
    if (!valid_) {
        if (!invalidReported_) {
            current_.diagnostic = NetworkDiagnostic::InvalidTiming;
            invalidReported_ = true;
        }
        return current_;
    }
    // A connection completed at or after the deadline is cancelled consistently.
    if (attempting_ && static_cast<std::uint32_t>(nowMs - attemptAt_) >= timing_.connectTimeoutMs)
        return fail(nowMs, NetworkDiagnostic::Timeout);
    const auto link = port_.status();
    if (link == NetworkLink::Connected) {
        const auto address = port_.address();
        if (address.empty()) return fail(nowMs, NetworkDiagnostic::MissingAddress);
        current_.diagnostic = !current_.connected ? NetworkDiagnostic::Connected :
            current_.address != address ? NetworkDiagnostic::AddressChanged : NetworkDiagnostic::None;
        current_.connected = true;
        current_.address = address;
        attempting_ = false;
        waiting_ = false;
        return current_;
    }
    if (current_.connected) return fail(nowMs, NetworkDiagnostic::Disconnected);
    if (attempting_) {
        if (link == NetworkLink::Disconnected) return fail(nowMs, NetworkDiagnostic::ConnectFailed);
        return current_;
    }
    if (waiting_ && static_cast<std::uint32_t>(nowMs - failedAt_) < timing_.retryDelayMs) return current_;
    waiting_ = false;
    attemptAt_ = nowMs;
    if (!port_.begin()) return fail(nowMs, NetworkDiagnostic::StartFailed);
    attempting_ = true;
    current_.diagnostic = NetworkDiagnostic::Connecting;
    return current_;
}
} // namespace esp32fan
