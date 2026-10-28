#include "adapters/StateApi.h"
#include <iostream>
#include <limits>
#include <locale>

using namespace esp32fan;

struct CommaDecimal : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    const std::string scenario = argv[1];
    FanController fan("test-boot");
    if (scenario == "healthy" || scenario == "invalid" || scenario == "stale") {
        fan.recordTemperature(31.125, 100);
        fan.setMode(Mode::Auto, CommandSource::Web);
        if (scenario == "invalid") fan.invalidateTemperature(200);
        if (scenario == "stale") fan.tick(5101);
    }
    auto state = fan.snapshot();
    if (scenario == "escape") state.bootId = std::string("quote\"slash\\newline\n") + '\0' + '\x01';
    if (scenario == "unsafe") state.revision = 9007199254740992ULL;
    if (scenario == "max-safe") state.revision = 9007199254740991ULL;
    if (scenario == "nan") { state.hasTemperature = true; state.hasSample = true; state.sensorStatus = SensorStatus::Ok; state.temperatureC = std::numeric_limits<double>::quiet_NaN(); }
    if (scenario == "bad-enum") state.mode = static_cast<Mode>(99);
    if (scenario == "empty") state.bootId.clear();
    if (scenario == "long-id") state.bootId.assign(129, 'a');
    if (scenario == "bad-utf8") state.bootId = "\xff";
    if (scenario == "unavailable-placeholder") state.temperatureC = std::numeric_limits<double>::quiet_NaN();
    if (scenario == "inconsistent") state.sensorStatus = SensorStatus::Ok;
    if (scenario == "bad-config") state.onThresholdC = 27;
    if (scenario == "healthy") std::locale::global(std::locale(std::locale::classic(), new CommaDecimal));
    const std::string method = scenario == "write" ? "PUT" : scenario == "long-method" ? std::string(17, 'A') : "GET";
    const std::string path = scenario == "missing" ? "/private-password-must-not-echo" :
        scenario == "long-path" ? std::string(129, 'x') : "/api/v1/state";
    const auto first = handleStateRequest(method, path, state);
    const auto second = handleStateRequest(method, path, state);
    if (first.body != second.body || (fan.snapshot().revision != state.revision && scenario == "healthy")) return 2;
    std::cout << first.status << '\n' << first.contentType << '\n' << first.cacheControl << '\n'
              << (first.allow ? first.allow : "-") << '\n' << first.body;
}
