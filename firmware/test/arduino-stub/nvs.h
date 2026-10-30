#pragma once

// Minimal ESP-IDF 5.5 API declarations for host fault injection only.
#include <cstddef>
#include <cstdint>

using esp_err_t = int;
using nvs_handle_t = std::uint32_t;
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_NVS_NOT_INITIALIZED = 0x1101;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
constexpr esp_err_t ESP_ERR_NVS_TYPE_MISMATCH = 0x1103;
constexpr esp_err_t ESP_ERR_NVS_NOT_ENOUGH_SPACE = 0x1105;
constexpr esp_err_t ESP_ERR_NVS_INVALID_HANDLE = 0x1107;
constexpr esp_err_t ESP_ERR_NVS_REMOVE_FAILED = 0x1108;
constexpr esp_err_t ESP_ERR_NVS_INVALID_STATE = 0x110b;
constexpr esp_err_t ESP_ERR_NVS_INVALID_LENGTH = 0x110c;
constexpr esp_err_t ESP_ERR_NVS_PART_NOT_FOUND = 0x110f;

extern "C" {
esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* out_handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out_value, std::size_t* length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, std::size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
}
