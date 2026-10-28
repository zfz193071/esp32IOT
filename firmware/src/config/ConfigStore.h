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
enum class SaveResult { Saved, Unchanged, Invalid, StorageFailure, NotLoaded };

class ConfigPersistence {
public:
    virtual ~ConfigPersistence() = default;
    virtual ReadResult read(ConfigRecord& record) = 0;
    // All-or-nothing contract: false MUST preserve the previously recoverable
    // record. A board adapter must verify this guarantee across reset/power loss.
    virtual bool replaceAtomically(const ConfigRecord& record) = 0;
};

class ConfigStore {
public:
    explicit ConfigStore(ConfigPersistence& persistence);
    // Startup only. Result is the diagnostic the App must log; no automatic write.
    LoadResult load();
    TemperatureConfig current() const;
    SaveResult save(const TemperatureConfig& config);

private:
    ConfigPersistence& persistence_;
    TemperatureConfig config_;
    bool loaded_ = false;
    bool writable_ = false;
};

} // namespace esp32fan
