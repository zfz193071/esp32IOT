#include "domain/FanController.h"

#include <cstdlib>
#include <iostream>

using namespace esp32fan;

void check(bool condition, const char* rule) {
    if (!condition) {
        std::cerr << "FAIL: " << rule << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool sameState(const DeviceState& a, const DeviceState& b) {
    return a.bootId == b.bootId && a.revision == b.revision &&
           a.mode == b.mode && a.fanOutputOn == b.fanOutputOn &&
           a.hasTemperature == b.hasTemperature && a.temperatureC == b.temperatureC &&
           a.sensorStatus == b.sensorStatus && a.hasSample == b.hasSample &&
           a.sampleAgeMs == b.sampleAgeMs && a.onThresholdC == b.onThresholdC &&
           a.offThresholdC == b.offThresholdC && a.fault == b.fault &&
           a.lastCommandSource == b.lastCommandSource && a.wifiConnected == b.wifiConnected;
}

int main() {
    FanController controller("test-boot-1");
    const auto initial = controller.snapshot();
    check(initial.mode == Mode::Manual && !initial.fanOutputOn && initial.revision == 0,
          "startup is manual/off at revision zero");
    check(!initial.hasTemperature && !initial.hasSample &&
              initial.sensorStatus == SensorStatus::Initializing && initial.fault == Fault::None,
          "missing temperature has no fabricated sample or manual-mode fault");
    check(initial.onThresholdC == 30 && initial.offThresholdC == 28,
          "startup thresholds match architecture");

    check(controller.setPower(true, CommandSource::Web) == CommandResult::Accepted,
          "explicit power command is accepted");
    const auto firstOn = controller.snapshot();
    for (int i = 0; i < 20; ++i) {
        controller.setPower(true, CommandSource::Web);
    }
    check(firstOn.fanOutputOn && sameState(firstOn, controller.snapshot()),
          "repeating identical ON commands does not toggle or advance revision");
    controller.setPower(false, CommandSource::Infrared);
    const auto firstOff = controller.snapshot();
    for (int i = 0; i < 20; ++i) {
        controller.setPower(false, CommandSource::Infrared);
    }
    check(!firstOff.fanOutputOn && sameState(firstOff, controller.snapshot()),
          "repeating identical OFF commands is idempotent");

    controller.setPower(true, CommandSource::Web);
    controller.toggleFromButton();
    controller.setPower(true, CommandSource::Infrared);
    check(controller.snapshot().fanOutputOn &&
              controller.snapshot().lastCommandSource == CommandSource::Infrared,
          "last accepted command wins across web/button/infrared");
    controller.toggleFromButton();
    check(!controller.snapshot().fanOutputOn &&
              controller.snapshot().lastCommandSource == CommandSource::Button,
          "button toggles current authoritative output");

    for (bool on : {false, true}) {
        controller.setPower(on, CommandSource::Web);
        const auto before = controller.snapshot();
        check(controller.setMode(Mode::Auto, CommandSource::Infrared) ==
                  CommandResult::TemperatureUnavailable,
              "auto mode requires temperature capability");
        check(sameState(before, controller.snapshot()),
              "rejected auto command leaves every state field unchanged");
        controller.setMode(Mode::Manual, CommandSource::Web);
        check(sameState(before, controller.snapshot()),
              "manual mode command preserves output and identical state revision");
    }

    auto before = controller.snapshot();
    controller.setPower(true, CommandSource::Infrared);
    check(controller.snapshot().fanOutputOn &&
              controller.snapshot().revision == before.revision + 1 &&
              controller.snapshot().lastCommandSource == CommandSource::Infrared,
          "source-only public state change advances revision");
    before = controller.snapshot();
    controller.setWifiConnected(true);
    check(controller.snapshot().revision == before.revision + 1 &&
              controller.snapshot().wifiConnected && controller.snapshot().fanOutputOn,
          "network status changes revision without changing power");
    before = controller.snapshot();
    controller.setWifiConnected(true);
    check(sameState(before, controller.snapshot()), "duplicate network event has no effect");
    controller.setWifiConnected(false);
    check(controller.snapshot().fanOutputOn && !controller.snapshot().wifiConnected &&
              controller.snapshot().lastCommandSource == before.lastCommandSource,
          "disconnect preserves output and last command source");

    before = controller.snapshot();
    check(controller.setMode(static_cast<Mode>(99), CommandSource::Web) ==
              CommandResult::InvalidCommand && sameState(before, controller.snapshot()),
          "unknown mode cannot mutate state");
    check(controller.setPower(false, static_cast<CommandSource>(99)) ==
              CommandResult::InvalidCommand && sameState(before, controller.snapshot()),
          "unknown source cannot mutate state");
    auto detached = controller.snapshot();
    detached.fanOutputOn = false;
    check(controller.snapshot().fanOutputOn, "snapshot cannot mutate controller state");

    FanController restarted("test-boot-2");
    check(!restarted.snapshot().fanOutputOn && restarted.snapshot().mode == Mode::Manual &&
              restarted.snapshot().revision == 0 &&
              restarted.snapshot().bootId != controller.snapshot().bootId,
          "new session starts manual/off using its injected boot identity");
    std::cout << "PASS: manual controller rules (host only; no hardware validation)\n";
}
