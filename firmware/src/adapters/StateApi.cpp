#include "StateApi.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace esp32fan {
namespace {
const char* modeName(Mode mode) {
    switch (mode) {
    case Mode::Manual: return "manual";
    case Mode::Auto: return "auto";
    }
    return nullptr;
}
const char* sensorName(SensorStatus status) {
    switch (status) {
    case SensorStatus::Initializing: return "initializing";
    case SensorStatus::Ok: return "ok";
    case SensorStatus::Invalid: return "invalid";
    case SensorStatus::Stale: return "stale";
    }
    return nullptr;
}
const char* faultName(Fault fault) {
    switch (fault) {
    case Fault::None: return "none";
    case Fault::TemperatureUnavailable: return "temperature_unavailable";
    }
    return nullptr;
}
const char* sourceName(CommandSource source) {
    switch (source) {
    case CommandSource::Button: return "button";
    case CommandSource::Infrared: return "infrared";
    case CommandSource::Web: return "web";
    case CommandSource::System: return "system";
    }
    return nullptr;
}

StateResponse error(int status, const char* code, const char* message) {
    StateResponse response;
    response.status = status;
    // Arguments are internal constants, never request text or private settings.
    response.body = std::string("{\"error\":{\"code\":\"") + code +
                    "\",\"message\":\"" + message + "\"}}";
    return response;
}

void writeString(std::ostream& out, const std::string& value) {
    const char* hex = "0123456789abcdef";
    out << '"';
    for (const unsigned char byte : value) {
        if (byte == '"' || byte == '\\') out << '\\' << static_cast<char>(byte);
        else if (byte < 0x20) out << "\\u00" << hex[byte >> 4] << hex[byte & 15];
        else out << static_cast<char>(byte);
    }
    out << '"';
}
} // namespace

StateResponse handleStateRequest(const std::string& method, const std::string& path,
                                const DeviceState& state) {
    if (path.size() > 128) return error(414, "PATH_TOO_LONG", "Path too long");
    if (method.size() > 16) return error(400, "INVALID_METHOD", "Invalid method");
    if (path != "/api/v1/state") return error(404, "NOT_FOUND", "Route not found");
    if (method != "GET") {
        auto response = error(405, "METHOD_NOT_ALLOWED", "Use GET");
        response.allow = "GET";
        return response;
    }
    TemperatureConfig config;
    config.onThresholdC = state.onThresholdC;
    config.offThresholdC = state.offThresholdC;
    bool asciiBoot = true;
    for (const unsigned char byte : state.bootId) if (byte > 0x7f) asciiBoot = false;
    const bool healthy = state.sensorStatus == SensorStatus::Ok;
    if (state.bootId.empty() || state.bootId.size() > 128 || !asciiBoot ||
        state.revision > 9007199254740991ULL || !validConfig(config) ||
        !modeName(state.mode) || !sensorName(state.sensorStatus) ||
        !faultName(state.fault) || !sourceName(state.lastCommandSource) ||
        (healthy && (!state.hasSample || !state.hasTemperature || state.sampleAgeMs > 5000)) ||
        (state.hasTemperature && (!healthy || !std::isfinite(state.temperatureC)))) {
        return error(503, "STATE_UNAVAILABLE", "State cannot be serialized");
    }

    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::boolalpha << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "{\"bootId\":";
    writeString(out, state.bootId);
    out << ",\"revision\":" << state.revision
        << ",\"mode\":\"" << modeName(state.mode) << '"'
        << ",\"fanOutputOn\":" << state.fanOutputOn << ",\"temperatureC\":";
    if (state.hasTemperature) out << state.temperatureC;
    else out << "null";
    out << ",\"sensorStatus\":\"" << sensorName(state.sensorStatus) << "\",\"sampleAgeMs\":";
    if (state.hasSample) out << state.sampleAgeMs;
    else out << "null";
    out << ",\"onThresholdC\":" << state.onThresholdC
        << ",\"offThresholdC\":" << state.offThresholdC
        << ",\"fault\":\"" << faultName(state.fault) << '"'
        << ",\"lastCommandSource\":\"" << sourceName(state.lastCommandSource) << '"'
        << ",\"wifiConnected\":" << state.wifiConnected << '}';
    StateResponse response;
    response.body = out.str();
    if (response.body.size() > 2048) return error(503, "STATE_UNAVAILABLE", "State too large");
    return response;
}

} // namespace esp32fan
