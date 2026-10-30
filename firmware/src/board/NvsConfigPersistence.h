#pragma once

#include "../config/ConfigStore.h"

namespace esp32fan {

// Default NVS partition, namespace "esp32fan", key "temperature".
// The platform must initialize NVS before startup load. This adapter never
// initializes, erases or repairs partitions. Use one instance per app session,
// exclusively from the main loop; do not share its namespace with other writers.
class NvsConfigPersistence final : public ConfigPersistence {
public:
    ReadResult read(ConfigRecord& record) override;
    PersistenceResult replace(const ConfigRecord& record) override;

private:
    bool uncertain_ = false;
};

} // namespace esp32fan
