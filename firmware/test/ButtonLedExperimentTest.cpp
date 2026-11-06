#include "board/ButtonLedExperiment.h"
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
    bool high = true;
    std::vector<std::string> events;
    void configureOutput(int pin, bool level) override {
        events.push_back("output " + std::to_string(pin) + (level ? " high" : " low"));
    }
    void configureInput(int pin, bool pullUp) override {
        events.push_back("input " + std::to_string(pin) + (pullUp ? " pullup" : " floating"));
    }
    bool read(int pin) override {
        check(pin == 27, "only the configured button may be read");
        return high;
    }
    void write(int pin, bool level) override {
        events.push_back("write " + std::to_string(pin) + (level ? " high" : " low"));
    }
};
int main() {
    BoardProfile profile;
    FakeGpio invalidGpio;
    ButtonLedExperiment missing(invalidGpio, profile, 20);
    check(!missing.initialize() && !missing.update(100) && invalidGpio.events.empty(),
          "missing pins disable all GPIO activity");
    profile.buttonGpio = 27;
    profile.statusLedGpio = 27;
    check(validateButtonLedProfile(profile) == ProfileError::GpioConflict, "shared pins rejected");
    profile.statusLedGpio = 34;
    check(validateButtonLedProfile(profile) == ProfileError::OutputOnInputOnlyGpio,
          "input-only LED pin rejected");
    profile.statusLedGpio = 6;
    check(validateButtonLedProfile(profile) == ProfileError::InvalidGpio, "flash pins rejected");
    profile.statusLedGpio = 25;
    profile.buttonGpio = 34;
    check(validateButtonLedProfile(profile) == ProfileError::InvalidGpio,
          "internal pull-up unavailable on input-only GPIO");
    profile.buttonGpio = 27;
    // Deliberately set a fan pin: this standalone experiment must never touch it.
    profile.fanEnableGpio = 23;
    for (bool inverted : {false, true}) {
        profile.buttonActiveLow = !inverted;
        profile.ledActiveHigh = !inverted;
        FakeGpio gpio;
        gpio.high = inverted; // Startup held.
        ButtonLedExperiment experiment(gpio, profile, 20);
        check(!experiment.update(0) && gpio.events.empty(), "no GPIO before initialization");
        check(experiment.initialize() && experiment.initialize(), "initialization is idempotent");
        check(gpio.events == std::vector<std::string>({inverted ? "output 25 high" : "output 25 low",
                inverted ? "input 27 floating" : "input 27 pullup"}),
              "only LED-off and button modes are configured");
        check(!experiment.update(0) && !experiment.update(3000) && !experiment.ledOn(),
              "startup held does not toggle LED");
        std::uint32_t now = 0xfffffff0u;
        gpio.high = !inverted;
        experiment.update(now);
        now += 20;
        check(!experiment.update(now), "stable release across timer wrap arms input");
        for (int press = 0; press < 20; ++press) {
            gpio.high = inverted;
            now += 1;
            check(!experiment.update(now), "new edge must debounce");
            gpio.high = !inverted;
            check(!experiment.update(now + 5), "bounce release ignored");
            gpio.high = inverted;
            check(!experiment.update(now + 10) && !experiment.update(now + 29),
                  "bounce restarts stable interval");
            now += 30;
            check(experiment.update(now), "stable press produces one LED transition");
            const bool on = (press % 2 == 0);
            check(experiment.ledOn() == on && gpio.events.back() ==
                      std::string("write 25 ") + ((on != inverted) ? "high" : "low"),
                  "LED follows configured polarity");
            now += 3000;
            check(!experiment.update(now), "three-second hold cannot repeat");
            gpio.high = !inverted;
            experiment.update(++now);
            now += 20;
            check(!experiment.update(now), "stable release does not toggle");
        }
        check(gpio.events.size() == 22 && !experiment.ledOn(),
              "20 presses yield exactly 20 writes and end off; fan untouched");
    }
    std::cout << "PASS: standalone button/LED experiment (host GPIO only)\n";
}
