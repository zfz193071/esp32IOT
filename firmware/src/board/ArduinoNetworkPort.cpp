#include "ArduinoNetworkPort.h"

#include <WiFi.h>

namespace esp32fan {
ArduinoNetworkPort::ArduinoNetworkPort(const char* ssid, const char* password)
    : ssid_(ssid), password_(password) {}

bool ArduinoNetworkPort::begin() {
    attempting_ = false;
    wasConnected_ = false;
    if (!ssid_ || !ssid_[0] || !password_ ||
        std::string(ssid_) == "YOUR_WIFI_SSID") return false;
    WiFi.setAutoReconnect(false);
    if (!WiFi.mode(WIFI_STA)) return false;
    const auto result = WiFi.begin(ssid_, password_);
    if (result == WL_CONNECT_FAILED || result == WL_NO_SSID_AVAIL) return false;
    attempting_ = true;
    return true;
}

NetworkLink ArduinoNetworkPort::status() {
    if (!attempting_) return NetworkLink::Disconnected;
    const auto link = WiFi.status();
    if (link == WL_CONNECTED) {
        if (address().empty()) return NetworkLink::Connecting;
        wasConnected_ = true;
        return NetworkLink::Connected;
    }
    if (link == WL_CONNECT_FAILED || link == WL_NO_SSID_AVAIL ||
        link == WL_CONNECTION_LOST || wasConnected_) return NetworkLink::Disconnected;
    // WL_DISCONNECTED is also reported while the first asynchronous attempt is in flight.
    // NetworkService owns the deadline for that ambiguous state.
    return NetworkLink::Connecting;
}

std::string ArduinoNetworkPort::address() {
    const String ip = WiFi.localIP().toString();
    return ip == "0.0.0.0" ? std::string() : std::string(ip.c_str());
}

void ArduinoNetworkPort::disconnect() {
    attempting_ = false;
    wasConnected_ = false;
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
}
} // namespace esp32fan
