#include "config/ConfigRecordCodec.h"
#include "domain/FanController.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

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

// Independent fixtures: b'FAN1' + Python struct.pack('<Idd', version, on, off),
// followed by struct.pack('<I', zlib.crc32(payload)). No production encoder used.
const EncodedConfigRecord golden{{
    0x46, 0x41, 0x4e, 0x31, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x41, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x40,
    0x16, 0x4a, 0x5f, 0x3c
}};
const EncodedConfigRecord future{{
    0x46, 0x41, 0x4e, 0x31, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x41, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x40,
    0xdc, 0x07, 0xf6, 0x93
}};
const EncodedConfigRecord nanRecord{{
    0x46, 0x41, 0x4e, 0x31, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x7f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x40,
    0x8c, 0xb9, 0x5a, 0xa5
}};
const EncodedConfigRecord invalidGap{{
    0x46, 0x41, 0x4e, 0x31, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3d, 0x40,
    0xa8, 0xe2, 0x5d, 0xd9
}};

// Byte-backed integration model. Atomic replacement is simulated, not Flash.
class BytePersistence : public ConfigPersistence {
public:
    std::vector<std::uint8_t> bytes;
    ReadResult status = ReadResult::Missing;
    bool failWrite = false;
    unsigned writes = 0;

    void seed(const EncodedConfigRecord& record) {
        bytes.assign(record.begin(), record.end());
        status = ReadResult::Found;
    }
    ReadResult read(ConfigRecord& record) override {
        return status == ReadResult::Found
            ? decodeConfigRecord(bytes.data(), bytes.size(), record) : status;
    }
    PersistenceResult replace(const ConfigRecord& record) override {
        ++writes;
        EncodedConfigRecord encoded;
        if (failWrite || !encodeConfigRecord(record, encoded)) return PersistenceResult::Failed;
        seed(encoded);
        return PersistenceResult::Saved;
    }
};

void expectCorrupt(const std::uint8_t* bytes, std::size_t size) {
    ConfigRecord destination;
    destination.version = 123;
    destination.thresholds = thresholds(50, 45);
    check(decodeConfigRecord(bytes, size, destination) == ReadResult::Corrupt &&
              destination.version == 123 &&
              sameConfig(destination.thresholds, thresholds(50, 45)),
          "bad framing or integrity never publishes a partial record");
}

int main() {
    ConfigRecord record;
    record.thresholds = thresholds(35.5, 32.25);
    EncodedConfigRecord encoded;
    check(encodeConfigRecord(record, encoded) && encoded == golden,
          "encoding matches independently generated endian and CRC fixture");
    ConfigRecord decoded;
    check(decodeConfigRecord(golden.data(), golden.size(), decoded) == ReadResult::Found &&
              decoded.version == 1 && sameConfig(decoded.thresholds, record.thresholds),
          "independent record restores the complete threshold pair");

    for (const auto& config : {thresholds(1, 0), thresholds(60, 59), thresholds(30, 28),
                              thresholds(35.1234567890123, 32.1234567890123),
                              thresholds(std::nextafter(30.0, 31.0), 28)}) {
        record.thresholds = config;
        check(encodeConfigRecord(record, encoded) &&
                  decodeConfigRecord(encoded.data(), encoded.size(), decoded) == ReadResult::Found &&
                  sameConfig(decoded.thresholds, config),
              "boundaries and binary64 fractional precision survive serialization");
    }

    for (const auto& config : {thresholds(61, 28), thresholds(30, -1), thresholds(28, 30),
                              thresholds(30, 29.5),
                              thresholds(std::numeric_limits<double>::quiet_NaN(), 28),
                              thresholds(30, std::numeric_limits<double>::infinity())}) {
        record.thresholds = config;
        encoded = golden;
        check(!encodeConfigRecord(record, encoded) && encoded == golden,
              "invalid thresholds cannot overwrite an encoded output buffer");
    }
    record.thresholds = thresholds(35.5, 32.25);
    for (const auto version : {0u, 2u, 0xffffffffu}) {
        record.version = version;
        encoded = golden;
        check(!encodeConfigRecord(record, encoded) && encoded == golden,
              "writer refuses unsupported record versions without changing output");
    }

    expectCorrupt(nullptr, 0);
    expectCorrupt(nullptr, golden.size());
    for (std::size_t size = 0; size < golden.size(); ++size) {
        expectCorrupt(golden.data(), size);
    }
    auto oversized = std::vector<std::uint8_t>(golden.begin(), golden.end());
    oversized.push_back(0);
    expectCorrupt(oversized.data(), oversized.size());
    for (std::size_t byte = 0; byte < golden.size(); ++byte) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            auto damaged = golden;
            damaged[byte] ^= static_cast<std::uint8_t>(1u << bit);
            expectCorrupt(damaged.data(), damaged.size());
        }
    }

    BytePersistence disk;
    ConfigStore initial(disk);
    check(initial.load() == LoadResult::MissingDefaults && disk.writes == 0,
          "absent byte record loads defaults without writing");
    check(initial.save(thresholds(35.5, 32.25)) == SaveResult::Saved &&
              disk.bytes == std::vector<std::uint8_t>(golden.begin(), golden.end()),
          "ConfigStore saves the specified complete byte record");
    check(initial.save(thresholds(35.5, 32.25)) == SaveResult::Unchanged && disk.writes == 1,
          "identical values do not rewrite the serialized record");
    disk.failWrite = true;
    check(initial.save(thresholds(40, 38)) == SaveResult::StorageFailure &&
              sameConfig(initial.current(), thresholds(35.5, 32.25)),
          "failed byte replacement preserves running thresholds");
    ConfigStore restarted(disk);
    check(restarted.load() == LoadResult::Loaded &&
              sameConfig(restarted.current(), thresholds(35.5, 32.25)),
          "new store restores previous bytes after simulated write failure");
    FanController controller("codec-restart", restarted.current());
    check(controller.snapshot().mode == Mode::Manual && !controller.snapshot().fanOutputOn,
          "restoring byte configuration does not restore power or automatic mode");

    for (const auto& fixture : {future, nanRecord, invalidGap}) {
        BytePersistence bad;
        bad.seed(fixture);
        check(decodeConfigRecord(bad.bytes.data(), bad.bytes.size(), decoded) == ReadResult::Found,
              "valid CRC is distinct from record version and value validation");
        ConfigStore fallback(bad);
        const auto expected = fixture == future ? LoadResult::VersionDefaults : LoadResult::CorruptDefaults;
        check(fallback.load() == expected && bad.writes == 0 &&
                  sameConfig(fallback.current(), thresholds(30, 28)),
              "valid CRC cannot bypass ConfigStore version or threshold validation");
    }
    disk.seed(golden);
    disk.bytes[12] ^= 1;
    const auto writesBeforeLoad = disk.writes;
    ConfigStore corrupt(disk);
    check(corrupt.load() == LoadResult::CorruptDefaults && disk.writes == writesBeforeLoad &&
              sameConfig(corrupt.current(), thresholds(30, 28)),
          "damaged serialized record falls back without automatic repair");
    disk.status = ReadResult::Unavailable;
    ConfigStore unavailable(disk);
    check(unavailable.load() == LoadResult::ReadFailureDefaults &&
              unavailable.save(thresholds(40, 38)) == SaveResult::StorageFailure &&
              disk.writes == writesBeforeLoad,
          "unavailable storage remains distinct from corrupt bytes and blocks writes");
    std::cout << "PASS: configuration byte format, corruption and recovery (host only)\n";
}
