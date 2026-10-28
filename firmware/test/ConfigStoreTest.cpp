#include "config/ConfigStore.h"
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

TemperatureConfig thresholds(double on, double off) {
    TemperatureConfig config;
    config.onThresholdC = on;
    config.offThresholdC = off;
    return config;
}

// Fault injection backend models atomic replacement; no flash claims.
class MemoryPersistence : public ConfigPersistence {
public:
    ConfigRecord record;
    ReadResult status = ReadResult::Missing;
    bool failWrite = false;
    unsigned writes = 0;
    ReadResult read(ConfigRecord& destination) override {
        if (status == ReadResult::Found) destination = record;
        return status;
    }
    bool replaceAtomically(const ConfigRecord& next) override {
        ++writes;
        if (failWrite) return false;
        record = next;
        status = ReadResult::Found;
        return true;
    }
};

int main() {
    MemoryPersistence disk;
    ConfigStore store(disk);
    check(store.save(thresholds(35, 32)) == SaveResult::NotLoaded && disk.writes == 0,
          "saving before startup load cannot overwrite unknown storage");
    check(store.load() == LoadResult::MissingDefaults &&
              sameConfig(store.current(), thresholds(30, 28)) && disk.writes == 0,
          "missing record yields defaults and diagnostic without writing");
    FanController controller("boot-1", store.current());
    controller.setPower(true, CommandSource::Web);
    const auto initialRevision = controller.snapshot().revision;
    check(controller.setConfig(store, thresholds(35.5, 32.25)) == SaveResult::Saved,
          "valid fractional thresholds persist as one complete record");
    check(disk.writes == 1 && disk.record.version == 1 &&
              sameConfig(disk.record.thresholds, store.current()) &&
              controller.snapshot().onThresholdC == 35.5 &&
              controller.snapshot().offThresholdC == 32.25 &&
              controller.snapshot().fanOutputOn &&
              controller.snapshot().revision == initialRevision + 1,
          "successful save publishes both values once and preserves manual output");
    check(controller.setConfig(store, thresholds(35.5, 32.25)) == SaveResult::Unchanged &&
              disk.writes == 1 && controller.snapshot().revision == initialRevision + 1,
          "identical explicit configuration has no writes or revision changes");

    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto infinity = std::numeric_limits<double>::infinity();
    for (const auto& config : {thresholds(61, 28), thresholds(30, -1), thresholds(-1, 0),
                              thresholds(30, 61), thresholds(30, 30), thresholds(30, 29.01),
                              thresholds(28, 30), thresholds(nan, 28), thresholds(30, nan),
                              thresholds(infinity, 28), thresholds(30, infinity)}) {
        check(controller.setConfig(store, config) == SaveResult::Invalid && disk.writes == 1 &&
                  sameConfig(store.current(), thresholds(35.5, 32.25)) &&
                  controller.snapshot().onThresholdC == 35.5 &&
                  controller.snapshot().offThresholdC == 32.25 &&
                  controller.snapshot().revision == initialRevision + 1,
              "invalid/nonfinite values cannot partially update runtime or persistence");
    }
    check(validConfig(thresholds(1, 0)) && validConfig(thresholds(60, 59)),
          "inclusive range endpoints and exactly one degree gap are valid");

    disk.failWrite = true;
    check(controller.setConfig(store, thresholds(40, 38)) == SaveResult::StorageFailure &&
              sameConfig(store.current(), thresholds(35.5, 32.25)) &&
              controller.snapshot().onThresholdC == 35.5 &&
              controller.snapshot().offThresholdC == 32.25 &&
              controller.snapshot().revision == initialRevision + 1,
          "failed atomic replacement preserves old runtime state and revision");
    ConfigStore rebootedStore(disk);
    check(rebootedStore.load() == LoadResult::Loaded &&
              sameConfig(rebootedStore.current(), thresholds(35.5, 32.25)),
          "after failed write a new instance recovers the previous complete record");
    FanController rebooted("boot-2", rebootedStore.current());
    check(rebooted.snapshot().mode == Mode::Manual && !rebooted.snapshot().fanOutputOn &&
              rebooted.snapshot().revision == 0 && rebooted.snapshot().onThresholdC == 35.5 &&
              rebooted.snapshot().offThresholdC == 32.25,
          "restart restores only thresholds, never mode or power");

    for (const auto status : {ReadResult::Missing, ReadResult::Corrupt, ReadResult::Unavailable}) {
        MemoryPersistence broken;
        broken.status = status;
        ConfigStore fallback(broken);
        const auto result = fallback.load();
        const auto expected = status == ReadResult::Missing ? LoadResult::MissingDefaults :
            status == ReadResult::Corrupt ? LoadResult::CorruptDefaults : LoadResult::ReadFailureDefaults;
        check(result == expected && sameConfig(fallback.current(), thresholds(30, 28)) &&
                  broken.writes == 0, "startup fallback identifies cause and never auto-repairs storage");
        if (status == ReadResult::Unavailable) {
            check(fallback.save(thresholds(40, 38)) == SaveResult::StorageFailure && broken.writes == 0,
                  "read failure blocks writes to unknown persisted state");
        }
    }
    for (const auto version : {0u, 2u}) {
        MemoryPersistence unsupported;
        unsupported.status = ReadResult::Found;
        unsupported.record.version = version;
        ConfigStore fallback(unsupported);
        check(fallback.load() == LoadResult::VersionDefaults && unsupported.writes == 0 &&
                  sameConfig(fallback.current(), thresholds(30, 28)),
              "unknown format version has explicit diagnostic and defaults");
    }
    MemoryPersistence malformed;
    malformed.status = ReadResult::Found;
    malformed.record.thresholds = thresholds(nan, 28);
    ConfigStore fallback(malformed);
    check(fallback.load() == LoadResult::CorruptDefaults && malformed.writes == 0 &&
              sameConfig(fallback.current(), thresholds(30, 28)),
          "record values are validated independently of backend integrity checks");
    check(fallback.save(thresholds(30, 28)) == SaveResult::Unchanged && malformed.writes == 0,
          "unchanged fallback values do not automatically repair corrupt storage");
    check(fallback.save(thresholds(40, 38)) == SaveResult::Saved && malformed.record.version == 1,
          "explicit changed config can replace a corrupt record");
    std::cout << "PASS: configuration validation, recovery and fault injection (host only)\n";
}
