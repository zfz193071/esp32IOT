#pragma once

// Copy to wifi.local.h and fill locally before compiling WiFiBringup.
// This example is not consumed by the host tests or preview server.
namespace esp32fan { namespace private_config {
constexpr char wifiSsid[] = "YOUR_WIFI_SSID";
constexpr char wifiPassword[] = "YOUR_WIFI_PASSWORD";
} }
