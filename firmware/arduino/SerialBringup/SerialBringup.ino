// ESP-004: minimal, motor-disconnected board identification sketch.
// This sketch does not assign any GPIO or read Wi-Fi credentials.
#include <Arduino.h>

namespace {
uint32_t lastReportMs = 0;

void reportIdentity() {
  Serial.println("ESP32_FAN_SERIAL_BRINGUP v2");
  Serial.print("Chip: ");
  Serial.println(ESP.getChipModel());
  Serial.print("Chip revision (major * 100 + minor): ");
  Serial.println(ESP.getChipRevision());
  Serial.print("Chip cores: ");
  Serial.println(ESP.getChipCores());
  Serial.print("Flash bytes (JEDEC ID derived): ");
  Serial.println(ESP.getFlashChipSize());
  Serial.print("Arduino core: ");
  Serial.println(ESP.getCoreVersion());
  Serial.print("ESP-IDF: ");
  Serial.println(ESP.getSdkVersion());
  Serial.println("Motor GPIO: unconfigured");
  Serial.println("Board model and wiring: not detected by this sketch");
  Serial.println("END_ESP32_FAN_SERIAL_BRINGUP");
}
}

void setup() {
  Serial.begin(115200);
  reportIdentity();
  lastReportMs = millis();
}

void loop() {
  const uint32_t now = millis();
  // Repeat for monitors attached after boot; no wait-for-serial startup loop.
  if (static_cast<uint32_t>(now - lastReportMs) >= 5000) {
    reportIdentity();
    lastReportMs = now;
  }
  delay(1);
}
