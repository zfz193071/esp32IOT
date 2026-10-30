#include "board/ArduinoGpioPort.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace esp32fan;
static std::vector<std::string> events;
static int readLevel = HIGH;
void digitalWrite(int gpio, int level) {
    events.push_back("write " + std::to_string(gpio) + " " + std::to_string(level));
}
int gpio_set_level(gpio_num_t gpio, unsigned int level) {
    events.push_back("preload " + std::to_string(gpio) + " " + std::to_string(level));
    return 0;
}
void pinMode(int gpio, int mode) {
    events.push_back("mode " + std::to_string(gpio) + " " + std::to_string(mode));
}
int digitalRead(int gpio) {
    events.push_back("read " + std::to_string(gpio));
    return readLevel;
}
static void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    ArduinoGpioPort gpio;
    gpio.configureOutput(23, false);
    check(events == std::vector<std::string>({"preload 23 0", "mode 23 2"}),
          "OFF level must be latched before output enable");
    events.clear();
    gpio.configureOutput(15, true);
    gpio.configureInput(27, true);
    gpio.configureInput(26, false);
    gpio.write(15, false);
    readLevel = LOW;
    check(!gpio.read(27), "LOW must map to false");
    check(events == std::vector<std::string>({"preload 15 1", "mode 15 2", "mode 27 4",
                                              "mode 26 3", "write 15 0", "read 27"}),
          "GPIO calls must use the requested modes and levels");
    std::cout << "PASS: Arduino GPIO call ordering (host stub only)\n";
}
