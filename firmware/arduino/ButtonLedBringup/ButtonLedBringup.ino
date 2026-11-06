// ESP-005 standalone experiment. Assign pins only after verifying actual wiring.
#include <Arduino.h>
#include "../../src/board/ArduinoGpioPort.h"
#include "../../src/board/ButtonLedExperiment.h"
#include "../../src/board/ArduinoGpioPort.cpp"
#include "../../src/board/BoardProfile.cpp"
#include "../../src/board/ButtonLedExperiment.cpp"
#include "../../src/input/ButtonDebouncer.cpp"

esp32fan::BoardProfile experimentProfile() {
  esp32fan::BoardProfile profile;
  profile.buttonGpio = -1;      // No default wiring assumptions.
  profile.statusLedGpio = -1;
  profile.buttonActiveLow = true;
  profile.ledActiveHigh = true;
  return profile;
}

esp32fan::ArduinoGpioPort experimentGpio;
// 20 ms is a software test value, not a measured hardware parameter.
esp32fan::ButtonLedExperiment experiment(experimentGpio, experimentProfile(), 20);

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32_FAN_BUTTON_LED_BRINGUP v1");
  Serial.println("Motor GPIO: unconfigured");
  if (!experiment.initialize()) {
    Serial.print("Button/LED disabled; profile error: ");
    Serial.println(static_cast<int>(experiment.error()));
  } else {
    Serial.println("Button/LED ready; release button before first press");
  }
}

void loop() {
  if (experiment.update(millis())) {
    Serial.println(experiment.ledOn() ? "Button press; LED ON" : "Button press; LED OFF");
  }
  delay(1);
}
