#include "board/BoardIoAdapter.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace esp32fan;
static void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
class FakeGpio : public GpioPort {
public:
    std::vector<std::string> events;
    bool level = true;
    void configureOutput(int gpio, bool high) override {
        events.push_back("output " + std::to_string(gpio) + (high ? " high" : " low"));
    }
    void configureInput(int gpio, bool pullUp) override {
        events.push_back("input " + std::to_string(gpio) + (pullUp ? " pullup" : " floating"));
    }
    bool read(int gpio) override {
        events.push_back("read " + std::to_string(gpio));
        return level;
    }
    void write(int gpio, bool high) override {
        events.push_back("write " + std::to_string(gpio) + (high ? " high" : " low"));
    }
};
int main() {
    FakeGpio gpio;
    BoardProfile profile;
    BoardIoAdapter invalid(gpio, profile);
    invalid.initializeOff();
    invalid.setFan(true);
    check(invalid.error() == ProfileError::MissingSignal && gpio.events.empty(),
          "invalid configuration must not touch hardware");

    profile.fanEnableGpio = 23;
    profile.buttonGpio = 27;
    profile.statusLedGpio = 15;
    profile.infraredGpio = 14;
    BoardIoAdapter adapter(gpio, profile);
    adapter.setFan(true);
    check(gpio.events.empty(), "writes before safe initialization must be ignored");
    adapter.initializeOff();
    adapter.initializeOff();
    check(gpio.events == std::vector<std::string>({"output 23 low", "output 15 low",
                                                   "input 27 pullup"}),
          "fan must be driven off first; setup must be idempotent");
    check(!adapter.buttonPressed(), "idle high means released");
    gpio.level = false;
    check(adapter.buttonPressed(), "pressed low is normalized");
    adapter.setFan(true);
    adapter.setIndicator(true);
    check(gpio.events.back() == "write 15 high" && gpio.events[gpio.events.size()-2] == "write 23 high",
          "fan and LED use their respective configured outputs");

    FakeGpio invertedGpio;
    profile.fanActiveHigh = false;
    profile.buttonActiveLow = false;
    profile.ledActiveHigh = false;
    BoardIoAdapter inverted(invertedGpio, profile);
    inverted.initializeOff();
    check(invertedGpio.events == std::vector<std::string>({"output 23 high", "output 15 high",
                                                           "input 27 floating"}),
          "profile polarities determine safe initial levels");
    check(inverted.buttonPressed(), "active-high button is normalized");
    inverted.setFan(true);
    check(invertedGpio.events.back() == "write 23 low", "active-low fan output is normalized");
    std::cout << "PASS: board GPIO adapter safe initialization and polarities (host only)\n";
}
