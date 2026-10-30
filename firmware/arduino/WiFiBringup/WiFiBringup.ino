// ESP-011: Wi-Fi-only serial bringup. No motor GPIO is configured here.
// Copy firmware/config/wifi.example.h to wifi.local.h before compiling.
#include <Arduino.h>
#include "../../config/wifi.local.h"
#include "../../src/board/ArduinoNetworkPort.h"
#include "../../src/services/NetworkService.h"

// Arduino IDE compiles sketch-local sources automatically, not sibling folders.
#include "../../src/board/ArduinoNetworkPort.cpp"
#include "../../src/services/NetworkService.cpp"

esp32fan::ArduinoNetworkPort networkPort(
    esp32fan::private_config::wifiSsid,
    esp32fan::private_config::wifiPassword);
esp32fan::NetworkService networkService(networkPort);

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32_FAN_WIFI_BRINGUP v1");
  Serial.println("Motor GPIO: unconfigured");
}

void loop() {
  const auto update = networkService.update(millis());
  using esp32fan::NetworkDiagnostic;
  switch (update.diagnostic) {
    case NetworkDiagnostic::None: break;
    case NetworkDiagnostic::Connecting: Serial.println("Wi-Fi: connecting"); break;
    case NetworkDiagnostic::Connected:
    case NetworkDiagnostic::AddressChanged:
      Serial.print("Wi-Fi IP: ");
      Serial.println(update.address.c_str());
      break;
    case NetworkDiagnostic::Disconnected: Serial.println("Wi-Fi: disconnected"); break;
    case NetworkDiagnostic::StartFailed: Serial.println("Wi-Fi: start failed"); break;
    case NetworkDiagnostic::ConnectFailed: Serial.println("Wi-Fi: connect failed"); break;
    case NetworkDiagnostic::Timeout: Serial.println("Wi-Fi: timeout"); break;
    case NetworkDiagnostic::MissingAddress: Serial.println("Wi-Fi: missing IP"); break;
    case NetworkDiagnostic::InvalidTiming: Serial.println("Wi-Fi: invalid timing"); break;
  }
  delay(10);
}
