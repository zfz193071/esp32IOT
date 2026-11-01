#include "board/Esp32BootId.h"
#include "adapters/StateApi.h"
#include <bootloader_random.h>
#include <esp_random.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

static void check(bool ok, const char* rule) {
    if (!ok) { std::cerr << "FAIL: " << rule << '\n'; std::exit(1); }
}
static bool entropyEnabled = false, peripheralsStarted = false;
static unsigned calls = 0;
static std::vector<char> order;
static std::uint8_t sample[16] = {
    0x00, 0x01, 0x0f, 0x10, 0x7f, 0x80, 0xab, 0xcd,
    0xef, 0xff, 0x35, 0x69, 0x02, 0xa0, 0xf0, 0xfe
};

void bootloader_random_enable() {
    check(!peripheralsStarted && !entropyEnabled, "entropy hardware only acquired during early startup");
    entropyEnabled = true;
    ++calls;
    order.push_back('E');
}
void esp_fill_random(void* buffer, std::size_t length) {
    check(entropyEnabled && !peripheralsStarted, "RF-off sampling has an active entropy source");
    check(length == sizeof(sample), "sample exactly 128 random bits");
    std::memcpy(buffer, sample, length);
    ++calls;
    order.push_back('F');
}
void bootloader_random_disable() {
    check(entropyEnabled, "release the acquired entropy source");
    entropyEnabled = false;
    ++calls;
    order.push_back('D');
}

int main(int argc, char** argv) {
    check(calls == 0, "no random hardware access in global constructors");
    check(argc == 2, "select a fresh-process boot sample");
    const std::string scenario = argv[1];
    std::string expected = "00010f107f80abcdefff356902a0f0fe";
    if (scenario == "second") { sample[15] = 0xfd; expected = "00010f107f80abcdefff356902a0f0fd"; }
    else if (scenario == "zero") { std::memset(sample, 0, sizeof(sample)); expected.assign(32, '0'); }
    else check(scenario == "first", "known boot scenario");

    const auto& id = esp32fan::esp32BootId();
    check(id == expected && id.size() == 32, "all bytes preserved as lowercase fixed-width hex");
    check(order == std::vector<char>({'E', 'F', 'D'}) && !entropyEnabled,
          "source released before returning identifier");
    peripheralsStarted = true;
    std::memset(sample, 0x55, sizeof(sample));
    for (unsigned i = 0; i < 1000; ++i)
        check(&esp32fan::esp32BootId() == &id && esp32fan::esp32BootId() == expected,
              "one cached ID throughout boot even after peripherals start");
    check(calls == 3, "cached reads never reconfigure or sample the random source");

    esp32fan::DeviceState state;
    state.bootId = id;
    const auto response = esp32fan::handleStateRequest("GET", "/api/v1/state", state);
    check(response.status == 200 && response.body.find("\"bootId\":\"" + expected + "\"") != std::string::npos,
          "generated ID accepted and preserved by real state API");
    std::cout << "PASS: ESP32 boot ID (injected " << scenario << " boot; no physical entropy validation)\n";
}
