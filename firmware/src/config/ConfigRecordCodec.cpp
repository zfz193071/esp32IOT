#include "ConfigRecordCodec.h"

#include <cstring>
#include <limits>

namespace esp32fan {
namespace {

static_assert(sizeof(double) == sizeof(std::uint64_t) &&
                  std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::digits == 53,
              "Config storage requires IEEE-754 binary64 doubles");

constexpr std::size_t checksumOffset = 24;
const std::uint8_t magic[] = {'F', 'A', 'N', '1'};

void writeLittleEndian(std::uint8_t* output, std::uint64_t value, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        output[i] = static_cast<std::uint8_t>(value & 0xff);
        value >>= 8;
    }
}

std::uint64_t readLittleEndian(const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
    }
    return value;
}

void writeDouble(std::uint8_t* output, double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    writeLittleEndian(output, bits, sizeof(bits));
}

double readDouble(const std::uint8_t* bytes) {
    const auto bits = readLittleEndian(bytes, sizeof(std::uint64_t));
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t checksum(const std::uint8_t* bytes) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < checksumOffset; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
        }
    }
    return crc ^ 0xffffffffu;
}

} // namespace

bool encodeConfigRecord(const ConfigRecord& record, EncodedConfigRecord& output) {
    if (record.version != 1 || !validConfig(record.thresholds)) return false;
    EncodedConfigRecord encoded{};
    std::memcpy(encoded.data(), magic, sizeof(magic));
    writeLittleEndian(encoded.data() + 4, record.version, 4);
    writeDouble(encoded.data() + 8, record.thresholds.onThresholdC);
    writeDouble(encoded.data() + 16, record.thresholds.offThresholdC);
    writeLittleEndian(encoded.data() + checksumOffset, checksum(encoded.data()), 4);
    output = encoded;
    return true;
}

ReadResult decodeConfigRecord(const std::uint8_t* bytes, std::size_t size,
                              ConfigRecord& output) {
    if (!bytes || size != EncodedConfigRecord{}.size() ||
        std::memcmp(bytes, magic, sizeof(magic)) != 0 ||
        readLittleEndian(bytes + checksumOffset, 4) != checksum(bytes)) {
        return ReadResult::Corrupt;
    }
    ConfigRecord decoded;
    decoded.version = static_cast<std::uint32_t>(readLittleEndian(bytes + 4, 4));
    decoded.thresholds.onThresholdC = readDouble(bytes + 8);
    decoded.thresholds.offThresholdC = readDouble(bytes + 16);
    output = decoded;
    return ReadResult::Found;
}

} // namespace esp32fan
