#include "NvsConfigPersistence.h"
#include "../config/ConfigRecordCodec.h"

#include <nvs.h>

namespace esp32fan {
namespace {
const char* const configNamespace = "esp32fan";
const char* const configKey = "temperature";
} // namespace

ReadResult NvsConfigPersistence::read(ConfigRecord& record) {
    // A read in the same uncertain session cannot establish durable recovery.
    if (uncertain_) return ReadResult::Unavailable;
    nvs_handle_t handle;
    const auto opened = nvs_open(configNamespace, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return ReadResult::Missing;
    if (opened != ESP_OK) return ReadResult::Unavailable;

    EncodedConfigRecord bytes{};
    std::size_t size = bytes.size();
    const auto result = nvs_get_blob(handle, configKey, bytes.data(), &size);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return ReadResult::Missing;
    if (result == ESP_ERR_NVS_TYPE_MISMATCH || result == ESP_ERR_NVS_INVALID_LENGTH)
        return ReadResult::Corrupt;
    if (result != ESP_OK) return ReadResult::Unavailable;
    return decodeConfigRecord(bytes.data(), size, record);
}

PersistenceResult NvsConfigPersistence::replace(const ConfigRecord& record) {
    if (uncertain_) return PersistenceResult::Unknown;
    EncodedConfigRecord bytes;
    if (!encodeConfigRecord(record, bytes)) return PersistenceResult::Failed;

    nvs_handle_t handle;
    if (nvs_open(configNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return PersistenceResult::Failed;

    auto result = nvs_set_blob(handle, configKey, bytes.data(), bytes.size());
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result == ESP_OK) return PersistenceResult::Saved;

    // NVS can report failure after the new value has reached flash (including
    // ESP_ERR_NVS_REMOVE_FAILED). Neither close nor a read proves rollback.
    // Conservatively classify any set/commit error as Unknown; never retry.
    uncertain_ = true;
    return PersistenceResult::Unknown;
}

} // namespace esp32fan
