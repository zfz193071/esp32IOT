#pragma once

#include <string>

enum wl_status_t {
    WL_IDLE_STATUS,
    WL_NO_SSID_AVAIL,
    WL_CONNECTED,
    WL_CONNECT_FAILED,
    WL_CONNECTION_LOST,
    WL_DISCONNECTED
};
enum wifi_mode_t { WIFI_STA };

class String {
public:
    explicit String(const char* value) : value_(value) {}
    const char* c_str() const { return value_.c_str(); }
    bool operator==(const char* other) const { return value_ == other; }
private:
    std::string value_;
};
class IPAddress {
public:
    explicit IPAddress(const char* value) : value_(value) {}
    String toString() const { return String(value_); }
private:
    const char* value_;
};
class WiFiClass {
public:
    bool setAutoReconnect(bool enabled);
    bool mode(wifi_mode_t mode);
    wl_status_t begin(const char* ssid, const char* password);
    wl_status_t status();
    IPAddress localIP();
    bool disconnect(bool wifioff, bool eraseap);
};
extern WiFiClass WiFi;
