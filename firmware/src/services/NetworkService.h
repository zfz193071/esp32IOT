#pragma once

#include <cstdint>
#include <string>

namespace esp32fan {

enum class NetworkLink { Disconnected, Connecting, Connected };
class NetworkPort {
public:
    virtual ~NetworkPort() = default;
    // Credentials belong to the board adapter, never to diagnostic events.
    // Calls must return promptly. begin starts one attempt, without a wait loop.
    virtual bool begin() = 0;
    virtual NetworkLink status() = 0;
    // Connected means DHCP/static address is ready; return the numeric IP only.
    virtual std::string address() = 0;
    // Cancel an attempt; disable driver auto-reconnect so this service owns pacing.
    virtual void disconnect() = 0;
};
struct NetworkTiming {
    std::uint32_t connectTimeoutMs = 10000;
    std::uint32_t retryDelayMs = 5000;
};
enum class NetworkDiagnostic { None, Connecting, Connected, AddressChanged, Disconnected,
                               StartFailed, ConnectFailed, Timeout, MissingAddress, InvalidTiming };
struct NetworkUpdate {
    bool connected = false;
    std::string address;
    NetworkDiagnostic diagnostic = NetworkDiagnostic::None;
};
class NetworkService {
public:
    explicit NetworkService(NetworkPort& port, NetworkTiming timing = NetworkTiming{});
    NetworkUpdate update(std::uint32_t nowMs);
private:
    NetworkUpdate fail(std::uint32_t nowMs, NetworkDiagnostic diagnostic);
    NetworkPort& port_;
    NetworkTiming timing_;
    bool valid_;
    bool invalidReported_ = false;
    bool attempting_ = false;
    bool waiting_ = false;
    std::uint32_t attemptAt_ = 0;
    std::uint32_t failedAt_ = 0;
    NetworkUpdate current_;
};
} // namespace esp32fan
