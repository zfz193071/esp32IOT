#include "Esp32BootId.h"
#include <bootloader_random.h>
#include <esp_random.h>
#include <cstdint>

namespace esp32fan {
namespace {
std::string generate() {
    std::uint8_t bytes[16]{};
    // IDF 5.5: RF-off esp_fill_random alone is only pseudo-random. Temporarily
    // enable entropy before other users of ADC/I2S/RF are initialized.
    bootloader_random_enable();
    esp_fill_random(bytes, sizeof(bytes));
    bootloader_random_disable();

    // Allocate/format only after releasing the shared entropy hardware.
    const char hex[] = "0123456789abcdef";
    char text[32];
    for (unsigned i = 0; i < sizeof(bytes); ++i) {
        text[i * 2] = hex[bytes[i] >> 4];
        text[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    return std::string(text, sizeof(text));
}
}

const std::string& esp32BootId() {
    static const std::string id = generate();
    return id;
}
} // namespace esp32fan
