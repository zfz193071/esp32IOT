#include "board/NvsConfigPersistence.h"
#include "config/ConfigRecordCodec.h"
#include "domain/FanController.h"
#include <nvs.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using namespace esp32fan;
void check(bool ok, const char* rule) {
    if (!ok) { std::cerr << "FAIL: " << rule << '\n'; std::exit(1); }
}
TemperatureConfig thresholds(double on, double off) {
    TemperatureConfig value;
    value.onThresholdC = on;
    value.offThresholdC = off;
    return value;
}
struct FakeNvs {
    std::vector<std::uint8_t> bytes;
    bool namespaceExists = false, keyExists = false, open = false;
    bool persistOnError = false;
    nvs_open_mode_t mode = NVS_READONLY;
    esp_err_t openError = ESP_OK, readError = ESP_OK, setError = ESP_OK, commitError = ESP_OK;
    unsigned opens = 0, reads = 0, sets = 0, commits = 0, closes = 0;
} disk;

esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* handle) {
    check(std::strcmp(name, "esp32fan") == 0 && !disk.open, "exclusive configuration namespace");
    ++disk.opens;
    if (disk.openError != ESP_OK) return disk.openError;
    if (!disk.namespaceExists && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    disk.namespaceExists = true;
    disk.open = true;
    disk.mode = mode;
    *handle = 42;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* value, std::size_t* length) {
    check(handle == 42 && disk.open && std::strcmp(key, "temperature") == 0 &&
              value != nullptr && *length == 28, "bounded blob read uses one fixed buffer");
    ++disk.reads;
    if (disk.readError != ESP_OK) return disk.readError;
    if (!disk.keyExists) return ESP_ERR_NVS_NOT_FOUND;
    if (*length < disk.bytes.size()) { *length = disk.bytes.size(); return ESP_ERR_NVS_INVALID_LENGTH; }
    *length = disk.bytes.size();
    if (*length) std::memcpy(value, disk.bytes.data(), *length);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, std::size_t length) {
    check(handle == 42 && disk.open && disk.mode == NVS_READWRITE &&
              std::strcmp(key, "temperature") == 0 && length == 28,
          "writes replace the complete configuration blob with a writable handle");
    ++disk.sets;
    // Model both failure outcomes, including immediate Flash mutation before an
    // error is returned. close/commit deliberately do not simulate a rollback.
    if ((disk.setError == ESP_OK && disk.commitError == ESP_OK) || disk.persistOnError) {
        const auto* bytes = static_cast<const std::uint8_t*>(value);
        disk.bytes.assign(bytes, bytes + length);
        disk.keyExists = true;
    }
    return disk.setError;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    check(handle == 42 && disk.open, "commit uses live handle");
    ++disk.commits;
    return disk.commitError;
}
void nvs_close(nvs_handle_t handle) {
    check(handle == 42 && disk.open, "close each successful open exactly once");
    disk.open = false;
    ++disk.closes;
}
void seed() {
    disk = FakeNvs{};
    ConfigRecord record;
    record.thresholds = thresholds(35.5, 32.25);
    EncodedConfigRecord bytes;
    check(encodeConfigRecord(record, bytes), "seed valid record");
    disk.bytes.assign(bytes.begin(), bytes.end());
    disk.namespaceExists = disk.keyExists = true;
}
void readFailure(ReadResult expected) {
    NvsConfigPersistence persistence;
    ConfigRecord out;
    out.version = 99;
    out.thresholds = thresholds(50, 45);
    check(persistence.read(out) == expected && out.version == 99 &&
              sameConfig(out.thresholds, thresholds(50, 45)) && !disk.open && disk.sets == 0,
          "failed read preserves destination and closes handle without repair writes");
}

int main() {
    readFailure(ReadResult::Missing);
    check(!disk.namespaceExists && disk.reads == 0 && disk.closes == 0,
          "missing namespace is not created by startup read");
    disk.namespaceExists = true;
    readFailure(ReadResult::Missing);
    check(disk.closes == 1, "missing key closes read-only handle");
    for (const auto error : {ESP_ERR_NVS_NOT_INITIALIZED, ESP_ERR_NVS_PART_NOT_FOUND, ESP_FAIL}) {
        seed(); disk.openError = error;
        readFailure(ReadResult::Unavailable);
        check(disk.reads == 0 && disk.closes == 0, "failed open never uses or closes invalid handle");
    }
    for (const auto error : {ESP_ERR_NVS_TYPE_MISMATCH, ESP_ERR_NVS_INVALID_LENGTH,
                            ESP_ERR_NVS_INVALID_HANDLE, ESP_ERR_NVS_INVALID_STATE, ESP_FAIL}) {
        seed(); disk.readError = error;
        readFailure(error == ESP_ERR_NVS_TYPE_MISMATCH || error == ESP_ERR_NVS_INVALID_LENGTH
                        ? ReadResult::Corrupt : ReadResult::Unavailable);
    }
    for (const auto length : {0u, 27u, 29u, 500u}) {
        seed(); disk.bytes.resize(length);
        readFailure(ReadResult::Corrupt);
    }
    seed(); disk.bytes[12] ^= 1;
    readFailure(ReadResult::Corrupt);

    seed();
    NvsConfigPersistence persistence;
    ConfigStore store(persistence);
    check(store.load() == LoadResult::Loaded &&
              sameConfig(store.current(), thresholds(35.5, 32.25)), "load and validate stored blob");
    check(store.save(thresholds(40, 38)) == SaveResult::Saved && disk.commits == 1 &&
              disk.sets == 1 && !disk.open, "success requires both set and commit");
    check(store.save(thresholds(40, 38)) == SaveResult::Unchanged && disk.sets == 1,
          "repeated thresholds do not write Flash");
    NvsConfigPersistence restartedPersistence;
    ConfigStore restarted(restartedPersistence);
    check(restarted.load() == LoadResult::Loaded &&
              sameConfig(restarted.current(), thresholds(40, 38)), "new session reads saved bytes");

    seed();
    NvsConfigPersistence invalid;
    ConfigRecord bad;
    bad.version = 2;
    check(invalid.replace(bad) == PersistenceResult::Failed && disk.opens == 0,
          "invalid record is a definite failure before touching NVS");
    disk.openError = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    bad.version = 1;
    const auto before = disk.bytes;
    check(invalid.replace(bad) == PersistenceResult::Failed && disk.bytes == before &&
              disk.sets == 0 && disk.closes == 0, "open failure cannot replace old record");

    for (bool reachesFlash : {false, true}) {
        for (bool failsCommit : {false, true}) {
            seed();
            NvsConfigPersistence failed;
            ConfigStore active(failed);
            active.load();
            FanController fan("before-restart", active.current());
            fan.recordTemperature(36, 0);
            fan.setMode(Mode::Auto, CommandSource::Web);
            const auto original = fan.snapshot();
            disk.persistOnError = reachesFlash;
            if (failsCommit) disk.commitError = ESP_FAIL;
            else disk.setError = ESP_ERR_NVS_REMOVE_FAILED;
            check(fan.setConfig(active, thresholds(40, 38)) == SaveResult::StorageUnknown &&
                      sameConfig(active.current(), thresholds(35.5, 32.25)) &&
                      fan.snapshot().onThresholdC == original.onThresholdC &&
                      fan.snapshot().offThresholdC == original.offThresholdC &&
                      fan.snapshot().mode == original.mode &&
                      fan.snapshot().fanOutputOn == original.fanOutputOn &&
                      fan.snapshot().lastCommandSource == original.lastCommandSource &&
                      fan.snapshot().configSaveBlocked && fan.snapshot().revision == original.revision + 1,
                  "uncertain save retains active control and publishes save block once");
            check(disk.sets == 1 && disk.commits == (failsCommit ? 1u : 0u) && !disk.open,
                  "set error skips commit; every path closes without retry");
            const auto opens = disk.opens;
            active.load();
            check(fan.setConfig(active, thresholds(35.5, 32.25)) == SaveResult::StorageUnknown &&
                      fan.setConfig(active, thresholds(45, 42)) == SaveResult::StorageUnknown &&
                      fan.snapshot().revision == original.revision + 1 && disk.opens == opens,
                  "reload and unchanged or changed submissions cannot clear session block");
            ConfigRecord output;
            check(failed.read(output) == ReadResult::Unavailable &&
                      failed.replace(output) == PersistenceResult::Unknown && disk.opens == opens,
                  "adapter also refuses uncertain session reads and retries");
            check(fan.setPower(false, CommandSource::Web) == CommandResult::Accepted &&
                      !fan.snapshot().fanOutputOn, "manual control still works while saves are blocked");

            // A new adapter/store models a reboot. Whether old or new bytes won
            // must be learned by reading, never inferred from the prior error.
            disk.setError = disk.commitError = ESP_OK;
            NvsConfigPersistence rebootPersistence;
            ConfigStore reboot(rebootPersistence);
            const auto expected = reachesFlash ? thresholds(40, 38) : thresholds(35.5, 32.25);
            check(reboot.load() == LoadResult::Loaded && sameConfig(reboot.current(), expected),
                  "restart recovers actual stored pair for both uncertain outcomes");
            FanController fresh("after-restart", reboot.current());
            check(!fresh.snapshot().configSaveBlocked && !fresh.snapshot().fanOutputOn &&
                      fresh.snapshot().mode == Mode::Manual && fresh.snapshot().revision == 0,
                  "restart restores only thresholds and releases the save block");
            check(fresh.setConfig(reboot, thresholds(45, 42)) == SaveResult::Saved,
                  "new session can explicitly save after recovery");
        }
    }
    std::cout << "PASS: NVS bounded reads, writes and uncertain recovery (host stub only)\n";
}
