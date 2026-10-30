// ESP-004: minimal, motor-disconnected board identification sketch.
// This sketch does not assign any GPIO or read Wi-Fi credentials.
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32_FAN_SERIAL_BRINGUP v1");
  Serial.print("Chip: ");
  Serial.println(ESP.getChipModel());
  Serial.print("Flash bytes: ");
  Serial.println(ESP.getFlashChipSize());
  Serial.println("Motor GPIO: unconfigured");
}

void loop() {}
