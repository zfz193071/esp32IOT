#include "input/ButtonDebouncer.h"
#include "domain/FanController.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace esp32fan;

void check(bool condition, const char* rule) {
    if (!condition) {
        std::cerr << "FAIL: " << rule << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int main() {
    // 20 ms is a test parameter, not a hardware measurement.
    ButtonDebouncer button(20);
    check(!button.update(false, 0) && !button.update(false, 20),
          "initial released input never emits a press");
    check(!button.update(true, 30) && !button.update(false, 35) &&
              !button.update(true, 40) && !button.update(true, 59),
          "press bounce restarts the full stability interval");
    check(button.update(true, 60), "press fires at the inclusive debounce boundary");
    for (std::uint32_t t = 61; t <= 3060; ++t) {
        check(!button.update(true, t), "three-second hold never repeats");
    }
    check(!button.update(false, 3070) && !button.update(true, 3075) &&
              !button.update(true, 3095),
          "brief release bounce does not rearm a held button");
    check(!button.update(false, 3100) && !button.update(false, 3120) &&
              !button.update(true, 3130) && button.update(true, 3150),
          "stable release allows the next press");

    ButtonDebouncer heldAtBoot(20);
    check(!heldAtBoot.update(true, 0) && !heldAtBoot.update(true, 3000),
          "startup-held input does not unexpectedly turn on the fan");
    check(!heldAtBoot.update(false, 3010) && !heldAtBoot.update(true, 3015) &&
              !heldAtBoot.update(true, 3035),
          "startup suppression survives a brief release glitch");
    check(!heldAtBoot.update(false, 3040) && !heldAtBoot.update(false, 3060) &&
              !heldAtBoot.update(true, 3070) && heldAtBoot.update(true, 3090),
          "startup-held input rearms only after stable release");

    ButtonDebouncer unsettled(20);
    check(!unsettled.update(false, 0) && !unsettled.update(true, 5) &&
              !unsettled.update(true, 25),
          "a short initial release cannot arm the input");

    ButtonDebouncer wrapped(20);
    const auto max = std::numeric_limits<std::uint32_t>::max();
    check(!wrapped.update(false, max - 100) && !wrapped.update(false, max - 80),
          "release before clock wrap arms input");
    check(!wrapped.update(true, max - 10) && !wrapped.update(true, 8) &&
              wrapped.update(true, 9) && !wrapped.update(true, 10),
          "uint32 clock wrap preserves inclusive debounce boundary");

    ButtonDebouncer immediate(0);
    check(!immediate.update(false, 0) && immediate.update(true, 1) &&
              !immediate.update(true, 1) && !immediate.update(false, 2) &&
              immediate.update(true, 3),
          "zero interval still emits only one event per press");

    ButtonDebouncer irregular(20);
    check(!irregular.update(false, 0) && !irregular.update(false, 100) &&
              !irregular.update(true, 105) && irregular.update(true, 180) &&
              !irregular.update(true, 500),
          "irregular polling uses elapsed time rather than sample counts");

    ButtonDebouncer repeated(20);
    FanController controller("button-integration-test");
    unsigned events = 0;
    auto sample = [&](bool pressed, std::uint32_t time) {
        if (repeated.update(pressed, time)) {
            ++events;
            controller.toggleFromButton();
        }
    };
    sample(false, 0);
    sample(false, 20);
    for (unsigned i = 0; i < 20; ++i) {
        const std::uint32_t start = 100 + i * 200;
        sample(true, start);
        sample(false, start + 3);
        sample(true, start + 6);
        sample(true, start + 25);
        check(events == i, "bouncing cycle cannot emit early");
        sample(true, start + 26);
        sample(true, start + 80);
        sample(false, start + 90);
        sample(true, start + 93);
        sample(false, start + 96);
        sample(false, start + 116);
        check(events == i + 1 && controller.snapshot().fanOutputOn == (i % 2 == 0),
              "each of twenty press/release cycles toggles controller exactly once");
    }
    check(controller.snapshot().revision == 20 &&
              controller.snapshot().lastCommandSource == CommandSource::Button,
          "debounced events use the existing authoritative controller");
    std::cout << "PASS: button debounce rules and controller integration (host only)\n";
}
