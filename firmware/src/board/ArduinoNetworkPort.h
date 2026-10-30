#pragma once

#include "../services/NetworkService.h"

namespace esp32fan {
// The caller owns the credential strings for the lifetime of this port.
class ArduinoNetworkPort final : public NetworkPort {
public:
    ArduinoNetworkPort(const char* ssid, const char* password);
    bool begin() override;
    NetworkLink status() override;
    std::string address() override;
    void disconnect() override;

private:
    const char* ssid_;
    const char* password_;
    bool attempting_ = false;
    bool wasConnected_ = false;
};
} // namespace esp32fan
