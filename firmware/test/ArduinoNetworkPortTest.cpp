#include "board/ArduinoNetworkPort.h"
#include <WiFi.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace esp32fan;
WiFiClass WiFi;
static std::vector<std::string> calls;
static bool modeOk = true;
static wl_status_t beginResult = WL_DISCONNECTED;
static wl_status_t link = WL_DISCONNECTED;
static const char* ip = "0.0.0.0";
bool WiFiClass::setAutoReconnect(bool enabled) {
    calls.push_back(enabled ? "auto on" : "auto off");
    return true;
}
bool WiFiClass::mode(wifi_mode_t mode) {
    calls.push_back(mode == WIFI_STA ? "station" : "wrong mode");
    return modeOk;
}
wl_status_t WiFiClass::begin(const char* ssid, const char* password) {
    if (std::string(ssid) != "test-network" || std::string(password) != "test-secret") {
        std::cerr << "FAIL: credentials passed incorrectly\n";
        std::exit(1);
    }
    calls.push_back("begin");
    return beginResult;
}
wl_status_t WiFiClass::status() { return link; }
IPAddress WiFiClass::localIP() { return IPAddress(ip); }
bool WiFiClass::disconnect(bool wifioff, bool eraseap) {
    calls.push_back((wifioff || eraseap) ? "destructive disconnect" : "disconnect");
    return true;
}
static void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    ArduinoNetworkPort empty("YOUR_WIFI_SSID", "placeholder");
    check(!empty.begin() && calls.empty(), "placeholder credentials must not start Wi-Fi");
    ArduinoNetworkPort port("test-network", "test-secret");
    check(port.begin(), "first attempt starts");
    check(calls == std::vector<std::string>({"auto off", "station", "begin"}),
          "auto reconnect disabled before connecting");
    check(port.status() == NetworkLink::Connecting, "initial disconnected means pending");
    link = WL_CONNECTED;
    check(port.status() == NetworkLink::Connecting, "IP must be ready before connected");
    ip = "192.168.1.8";
    check(port.status() == NetworkLink::Connected && port.address() == ip, "report numeric IP");
    link = WL_DISCONNECTED;
    check(port.status() == NetworkLink::Disconnected, "drop after success is disconnected");
    port.disconnect();
    check(calls[calls.size() - 2] == "auto off" && calls.back() == "disconnect",
          "cancel must not erase credentials or leave automatic reconnect on");
    check(port.status() == NetworkLink::Disconnected, "cancelled port is disconnected");
    beginResult = WL_NO_SSID_AVAIL;
    check(!port.begin(), "immediate driver failure is reported");
    beginResult = WL_DISCONNECTED;
    check(port.begin(), "retry can start");
    link = WL_CONNECT_FAILED;
    check(port.status() == NetworkLink::Disconnected, "asynchronous failure is reported");
    std::cout << "PASS: Arduino Wi-Fi port state mapping (host stub only)\n";
}
