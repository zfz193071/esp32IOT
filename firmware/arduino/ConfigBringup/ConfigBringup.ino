// ESP-018: serial-only NVS experiment. No motor GPIO is configured.
// Arduino-ESP32 initializes the default NVS partition before setup().
// This sketch does not erase storage or automatically write configuration.
// Core startup may erase/reinitialize damaged NVS; see config-store.md.
#include <Arduino.h>
#include "../../src/board/NvsConfigPersistence.h"
#include "../../src/board/NvsConfigPersistence.cpp"
#include "../../src/config/ConfigRecordCodec.cpp"
#include "../../src/config/ConfigStore.cpp"

esp32fan::NvsConfigPersistence persistence;
esp32fan::ConfigStore configStore(persistence);

void printThresholds() {
  const auto config = configStore.current();
  Serial.print("Current on/off C: ");
  Serial.print(config.onThresholdC, 8);
  Serial.print(" / ");
  Serial.println(config.offThresholdC, 8);
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32_FAN_CONFIG_BRINGUP v1");
  Serial.println("Motor GPIO: unconfigured");
  using esp32fan::LoadResult;
  switch (configStore.load()) {
    case LoadResult::Loaded: Serial.println("Config: loaded"); break;
    case LoadResult::MissingDefaults: Serial.println("Config: missing, defaults"); break;
    case LoadResult::CorruptDefaults: Serial.println("Config: corrupt, defaults"); break;
    case LoadResult::VersionDefaults: Serial.println("Config: unsupported version, defaults"); break;
    case LoadResult::ReadFailureDefaults: Serial.println("Config: read failure, saves disabled"); break;
  }
  printThresholds();
  Serial.println("s: save 35.5/32.25; d: save 30/28; p: print current thresholds");
  Serial.println("After a confirmed save, restart the board and check the loaded values.");
}

void loop() {
  if (Serial.available() > 0) {
    const int command = Serial.read();
    if (command == 'p') printThresholds();
    if (command == 's' || command == 'd') {
      esp32fan::TemperatureConfig config;
      if (command == 's') {
        config.onThresholdC = 35.5;
        config.offThresholdC = 32.25;
      }
      using esp32fan::SaveResult;
      switch (configStore.save(config)) {
        case SaveResult::Saved: Serial.println("Save: confirmed"); break;
        case SaveResult::Unchanged: Serial.println("Save: unchanged, no write"); break;
        case SaveResult::StorageUnknown:
          Serial.println("Save: UNKNOWN, further saves blocked; restart to verify stored values"); break;
        case SaveResult::StorageFailure: Serial.println("Save: failed, old record retained"); break;
        case SaveResult::Invalid: Serial.println("Save: invalid thresholds"); break;
        case SaveResult::NotLoaded: Serial.println("Save: startup load required"); break;
      }
      printThresholds();
    }
  }
  delay(10);
}
