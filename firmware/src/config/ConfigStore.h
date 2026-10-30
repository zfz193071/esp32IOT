#pragma once

#include <cstdint>

namespace esp32fan {

struct TemperatureConfig {
    double onThresholdC = 30;
    double offThresholdC = 28;
};

bool validConfig(const TemperatureConfig& config);
bool sameConfig(const TemperatureConfig& a, const TemperatureConfig& b);

// Logical record, not a raw struct-to-flash format. Adapter owns serialization
// and integrity verification. Mode/power are deliberately absent.
struct ConfigRecord {
    std::uint32_t version = 1;
    TemperatureConfig thresholds;
};

enum class ReadResult { Found, Missing, Corrupt, Unavailable };
enum class LoadResult { Loaded, MissingDefaults, CorruptDefaults, VersionDefaults, ReadFailureDefaults };
enum class PersistenceResult { Saved, Failed, Unknown };
enum class SaveResult { Saved, Unchanged, Invalid, StorageFailure, StorageUnknown, NotLoaded };

class ConfigPersistence {
public:
    virtual ~ConfigPersistence() = default;
    virtual ReadResult read(ConfigRecord& record) = 0;
    // Failed guarantees the old recoverable record is unchanged. Unknown means
    // the write may have reached storage; never treat it as a rollback or retry.
    // Saved acknowledges a complete record; board power-loss tests are required.
    virtual PersistenceResult replace(const ConfigRecord& record) = 0;
};

class ConfigStore {
public:
    explicit ConfigStore(ConfigPersistence& persistence);
    // Startup only, idempotent. Repeated calls neither reload nor unlock saves.
    LoadResult load();
    TemperatureConfig current() const;
    SaveResult save(const TemperatureConfig& config);

private:
    ConfigPersistence& persistence_;
    TemperatureConfig config_;
    bool loaded_ = false;
    bool writable_ = false;
    bool uncertain_ = false;
    LoadResult loadResult_ = LoadResult::ReadFailureDefaults;
};

} // namespace esp32fan
