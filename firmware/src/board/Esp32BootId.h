#pragma once
#include <string>

namespace esp32fan {
// Call once during early setup, BEFORE ADC, I2S, Wi-Fi or Bluetooth starts.
// The first call temporarily owns the internal ADC entropy source. Later calls
// return the same cached 128-bit identifier without touching peripherals.
// Not a credential or a mathematically guaranteed unique sequence number.
const std::string& esp32BootId();
} // namespace esp32fan
