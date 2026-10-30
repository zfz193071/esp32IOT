#include "ArduinoGpioPort.h"

#include <Arduino.h>
#include <driver/gpio.h>

namespace esp32fan {
void ArduinoGpioPort::configureOutput(int gpio, bool initialHigh) {
    // Preload the output latch before switching the pin to output mode.
    // Reset/boot behavior before setup() still needs hardware validation.
    gpio_set_level(static_cast<gpio_num_t>(gpio), initialHigh ? 1U : 0U);
    pinMode(gpio, OUTPUT);
}

void ArduinoGpioPort::configureInput(int gpio, bool pullUp) {
    pinMode(gpio, pullUp ? INPUT_PULLUP : INPUT);
}

bool ArduinoGpioPort::read(int gpio) { return digitalRead(gpio) == HIGH; }

void ArduinoGpioPort::write(int gpio, bool high) {
    digitalWrite(gpio, high ? HIGH : LOW);
}
} // namespace esp32fan
