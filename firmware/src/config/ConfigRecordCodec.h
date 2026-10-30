#pragma once

#include "ConfigStore.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace esp32fan {

// FAN1 magic, uint32 version, two binary64 thresholds, CRC-32/ISO-HDLC.
// All numeric fields are little-endian; no native struct layout is persisted.
using EncodedConfigRecord = std::array<std::uint8_t, 28>;

// Only valid version-1 records can be written. Failure leaves output unchanged.
bool encodeConfigRecord(const ConfigRecord& record, EncodedConfigRecord& output);

// Checks framing and integrity only. ConfigStore owns version/value validation.
// Found publishes a complete record; Corrupt leaves output unchanged.
// Storage adapters must distinguish Missing/Unavailable before calling this.
ReadResult decodeConfigRecord(const std::uint8_t* bytes, std::size_t size,
                              ConfigRecord& output);

} // namespace esp32fan
