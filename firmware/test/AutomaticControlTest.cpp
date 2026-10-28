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

class MemoryPersistence : public ConfigPersistence {
public:
    bool fail = false;
    ReadResult read(ConfigRecord&) override { return ReadResult::Missing; }
    bool replaceAtomically(const ConfigRecord&) override { return !fail; }
};

int main() {
    FanController fan("auto-test");
    check(fan.setMode(Mode::Auto, CommandSource::Web) == CommandResult::TemperatureUnavailable,
          "no sample prevents automatic admission");
    fan.setPower(true, CommandSource::Web);
    fan.recordTemperature(29, 0);
    check(fan.setMode(Mode::Auto, CommandSource::Web) == CommandResult::Accepted &&
              !fan.snapshot().fanOutputOn, "enter auto in deadband starts from off despite manual on");
    fan.recordTemperature(30, 1000);
    check(fan.snapshot().fanOutputOn, "on threshold is inclusive");
    fan.recordTemperature(29, 2000);
    check(fan.snapshot().fanOutputOn, "deadband retains on output");
    fan.setMode(Mode::Auto, CommandSource::Web);
    const auto repeatedRevision = fan.snapshot().revision;
    fan.setMode(Mode::Auto, CommandSource::Web);
    check(fan.snapshot().fanOutputOn && fan.snapshot().revision == repeatedRevision,
          "repeated auto does not reset hysteresis or revision");
    fan.recordTemperature(28, 3000);
    check(!fan.snapshot().fanOutputOn, "off threshold is inclusive");
    fan.recordTemperature(29.99, 4000);
    check(!fan.snapshot().fanOutputOn, "just below on threshold stays off");
    fan.recordTemperature(30, 5000);
    fan.recordTemperature(28.01, 6000);
    check(fan.snapshot().fanOutputOn, "just above off threshold stays on");
    fan.setMode(Mode::Manual, CommandSource::Infrared);
    check(fan.snapshot().fanOutputOn && fan.snapshot().mode == Mode::Manual,
          "manual mode preserves on output");
    fan.recordTemperature(10, 7000);
    check(fan.snapshot().fanOutputOn, "manual ignores temperature thresholds");
    fan.setMode(Mode::Auto, CommandSource::Web);
    fan.setPower(true, CommandSource::Button);
    check(fan.snapshot().mode == Mode::Manual && fan.snapshot().fanOutputOn,
          "explicit manual on takes over automatic off");
    fan.recordTemperature(40, 8000);
    fan.setMode(Mode::Auto, CommandSource::Web);
    fan.setPower(false, CommandSource::Infrared);
    check(fan.snapshot().mode == Mode::Manual && !fan.snapshot().fanOutputOn,
          "explicit manual off takes over automatic on");

    fan.setMode(Mode::Auto, CommandSource::Web);
    const auto healthyRevision = fan.snapshot().revision;
    fan.tick(13000);
    check(fan.snapshot().sampleAgeMs == 5000 && fan.snapshot().sensorStatus == SensorStatus::Ok &&
              fan.snapshot().revision == healthyRevision && fan.snapshot().fanOutputOn,
          "exactly 5000ms remains healthy and age alone does not advance revision");
    fan.tick(13001);
    check(fan.snapshot().mode == Mode::Manual && !fan.snapshot().fanOutputOn &&
              fan.snapshot().sensorStatus == SensorStatus::Stale && !fan.snapshot().hasTemperature &&
              fan.snapshot().fault == Fault::TemperatureUnavailable &&
              fan.snapshot().lastCommandSource == CommandSource::System &&
              fan.snapshot().revision == healthyRevision + 1,
          "stale transition atomically closes output, exits auto and records fault");
    const auto staleRevision = fan.snapshot().revision;
    fan.tick(14000);
    check(fan.setMode(Mode::Auto, CommandSource::Web) == CommandResult::TemperatureUnavailable &&
              fan.snapshot().revision == staleRevision, "stale auto rejection does not change state");
    fan.recordTemperature(40, 15000);
    check(fan.snapshot().fault == Fault::None && fan.snapshot().mode == Mode::Manual &&
              !fan.snapshot().fanOutputOn, "recovery clears fault without restarting auto or output");
    const auto recoveredRevision = fan.snapshot().revision;
    fan.recordTemperature(40, 16000);
    check(fan.snapshot().revision == recoveredRevision && fan.snapshot().sampleAgeMs == 0,
          "identical fresh sample resets age without business revision");

    for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        fan.recordTemperature(40, 17000);
        fan.setMode(Mode::Auto, CommandSource::Web);
        fan.recordTemperature(bad, 17001);
        check(fan.snapshot().sensorStatus == SensorStatus::Invalid && !fan.snapshot().fanOutputOn &&
                  fan.snapshot().mode == Mode::Manual && !fan.snapshot().hasTemperature &&
                  fan.snapshot().fault == Fault::TemperatureUnavailable,
              "nonfinite samples follow invalid sensor fault path");
        check(fan.setMode(Mode::Auto, CommandSource::Web) == CommandResult::TemperatureUnavailable,
              "invalid samples cannot admit automatic mode");
    }
    fan.setPower(true, CommandSource::Button);
    fan.invalidateTemperature(17002);
    check(fan.snapshot().fanOutputOn && fan.snapshot().mode == Mode::Manual,
          "manual operation remains possible during sensor fault");
    fan.recordTemperature(25, 18000);
    check(fan.snapshot().fanOutputOn && fan.snapshot().fault == Fault::None,
          "recovery does not override a later manual command");

    FanController cold("cold");
    cold.invalidateTemperature(0);
    check(!cold.snapshot().hasSample && cold.snapshot().fault == Fault::None,
          "invalid sensor in manual mode does not invent an automatic-control fault");
    cold.recordTemperature(29, 1);
    cold.setMode(Mode::Auto, CommandSource::Web);
    cold.invalidateTemperature(2);
    check(cold.snapshot().mode == Mode::Manual && cold.snapshot().fault == Fault::TemperatureUnavailable,
          "sensor failure exits auto even if output was already off");

    FanController wrapped("wrapped");
    const auto max = std::numeric_limits<std::uint32_t>::max();
    wrapped.recordTemperature(40, max - 100);
    wrapped.setMode(Mode::Auto, CommandSource::Web);
    wrapped.tick(4899);
    check(wrapped.snapshot().sampleAgeMs == 5000 && wrapped.snapshot().fanOutputOn,
          "sample age handles uint32 wrap at exact freshness boundary");
    wrapped.tick(4900);
    check(!wrapped.snapshot().fanOutputOn && wrapped.snapshot().sensorStatus == SensorStatus::Stale,
          "sample expires after clock wrap");

    MemoryPersistence disk;
    ConfigStore store(disk);
    store.load();
    FanController configured("configured", store.current());
    configured.recordTemperature(29, 0);
    configured.setMode(Mode::Auto, CommandSource::Web);
    TemperatureConfig config;
    config.onThresholdC = 29;
    config.offThresholdC = 27;
    const auto beforeConfig = configured.snapshot().revision;
    check(configured.setConfig(store, config) == SaveResult::Saved &&
              configured.snapshot().fanOutputOn && configured.snapshot().revision == beforeConfig + 1,
          "saved thresholds immediately recalculate output with a single revision");
    config.onThresholdC = 32;
    config.offThresholdC = 30;
    disk.fail = true;
    const auto beforeFailure = configured.snapshot().revision;
    check(configured.setConfig(store, config) == SaveResult::StorageFailure &&
              configured.snapshot().fanOutputOn && configured.snapshot().onThresholdC == 29 &&
              configured.snapshot().revision == beforeFailure,
          "failed persistence cannot recalculate using uncommitted thresholds");
    disk.fail = false;
    configured.setConfig(store, config);
    check(!configured.snapshot().fanOutputOn, "saved off threshold immediately switches off");
    configured.recordTemperature(40, 1);
    configured.setWifiConnected(true);
    configured.setWifiConnected(false);
    check(configured.snapshot().mode == Mode::Auto && configured.snapshot().fanOutputOn,
          "Wi-Fi disconnect does not interrupt valid automatic operation");

    // Required loop ordering: health first, commands second; commands cannot admit stale auto.
    configured.tick(5002);
    check(configured.setMode(Mode::Auto, CommandSource::Web) == CommandResult::TemperatureUnavailable,
          "health update before commands prevents stale automatic admission");
    configured.setPower(true, CommandSource::Button);
    check(configured.snapshot().mode == Mode::Manual && configured.snapshot().fanOutputOn,
          "manual command following health failure wins within the same loop");
    std::cout << "PASS: automatic hysteresis, health, manual takeover and configuration (host only)\n";
}
