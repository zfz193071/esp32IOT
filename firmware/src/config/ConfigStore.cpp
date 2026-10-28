#include "ConfigStore.h"

#include <cmath>

namespace esp32fan {

bool validConfig(const TemperatureConfig& config) {
    return std::isfinite(config.onThresholdC) && std::isfinite(config.offThresholdC) &&
           config.onThresholdC >= 0 && config.onThresholdC <= 60 &&
           config.offThresholdC >= 0 && config.offThresholdC <= 60 &&
           config.onThresholdC - config.offThresholdC >= 1;
}

bool sameConfig(const TemperatureConfig& a, const TemperatureConfig& b) {
    return a.onThresholdC == b.onThresholdC && a.offThresholdC == b.offThresholdC;
}

ConfigStore::ConfigStore(ConfigPersistence& persistence) : persistence_(persistence) {}

LoadResult ConfigStore::load() {
    ConfigRecord record;
    const auto result = persistence_.read(record);
    config_ = TemperatureConfig{};
    loaded_ = true;
    writable_ = result != ReadResult::Unavailable;
    switch (result) {
    case ReadResult::Missing:
        return LoadResult::MissingDefaults;
    case ReadResult::Corrupt:
        return LoadResult::CorruptDefaults;
    case ReadResult::Unavailable:
        return LoadResult::ReadFailureDefaults;
    case ReadResult::Found:
        if (record.version != 1) return LoadResult::VersionDefaults;
        if (!validConfig(record.thresholds)) return LoadResult::CorruptDefaults;
        config_ = record.thresholds;
        return LoadResult::Loaded;
    }
    writable_ = false;
    return LoadResult::ReadFailureDefaults;
}

TemperatureConfig ConfigStore::current() const { return config_; }

SaveResult ConfigStore::save(const TemperatureConfig& config) {
    if (!validConfig(config)) return SaveResult::Invalid;
    if (!loaded_) return SaveResult::NotLoaded;
    if (!writable_) return SaveResult::StorageFailure;
    if (sameConfig(config, config_)) return SaveResult::Unchanged;
    ConfigRecord record;
    record.thresholds = config;
    if (!persistence_.replaceAtomically(record)) return SaveResult::StorageFailure;
    config_ = config;
    return SaveResult::Saved;
}

} // namespace esp32fan
