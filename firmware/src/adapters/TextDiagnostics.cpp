#include "TextDiagnostics.h"

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace esp32fan {
namespace {
// Keep externally supplied identifiers on one bounded, printable log line.
std::string token(const std::string& value) {
    std::string result;
    for (unsigned char c : value) {
        if (result.size() == 128) break;
        result += (c > 32 && c < 127 && c != '=') ? static_cast<char>(c) : '_';
    }
    return result.empty() ? "-" : result;
}
const char* source(CommandSource value) {
    switch (value) {
    case CommandSource::Button: return "button";
    case CommandSource::Infrared: return "infrared";
    case CommandSource::Web: return "web";
    case CommandSource::System: return "system";
    }
    return "unknown";
}
const char* load(LoadResult value) {
    switch (value) {
    case LoadResult::Loaded: return "loaded";
    case LoadResult::MissingDefaults: return "missing_defaults";
    case LoadResult::CorruptDefaults: return "corrupt_defaults";
    case LoadResult::VersionDefaults: return "version_defaults";
    case LoadResult::ReadFailureDefaults: return "read_failure_defaults";
    }
    return "unknown";
}
std::string stateFields(const DeviceState& state) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << "boot=" << token(state.bootId) << " revision=" << state.revision
        << " mode=" << (state.mode == Mode::Auto ? "auto" : "manual")
        << " output=" << (state.fanOutputOn ? "on" : "off")
        << " source=" << source(state.lastCommandSource)
        << " fault=" << (state.fault == Fault::None ? "none" : "temperature_unavailable")
        << " on_c=" << state.onThresholdC << " off_c=" << state.offThresholdC;
    return out.str();
}
} // namespace

void TextDiagnostics::emit(const std::string& line) {
    if (!sink_.tryWriteLine(line) && dropped_ != std::numeric_limits<std::uint32_t>::max()) ++dropped_;
}
void TextDiagnostics::startup(LoadResult result, const DeviceState& state) {
    emit(std::string("startup config=") + load(result) + " " + stateFields(state));
}
void TextDiagnostics::stateChanged(const DeviceState& state) {
    emit("state " + stateFields(state));
}
void TextDiagnostics::temperature(TemperatureDiagnostic diagnostic) {
    const char* name = "unknown";
    switch (diagnostic) {
    case TemperatureDiagnostic::None: return;
    case TemperatureDiagnostic::StartFailed: name = "start_failed"; break;
    case TemperatureDiagnostic::ReadFailed: name = "read_failed"; break;
    case TemperatureDiagnostic::NonFinite: name = "non_finite"; break;
    case TemperatureDiagnostic::Timeout: name = "timeout"; break;
    case TemperatureDiagnostic::InvalidTiming: name = "invalid_timing"; break;
    }
    emit(std::string("temperature event=") + name);
}
void TextDiagnostics::network(const NetworkUpdate& update) {
    const char* name = "unknown";
    switch (update.diagnostic) {
    case NetworkDiagnostic::None: return;
    case NetworkDiagnostic::Connecting: name = "connecting"; break;
    case NetworkDiagnostic::Connected: name = "connected"; break;
    case NetworkDiagnostic::AddressChanged: name = "address_changed"; break;
    case NetworkDiagnostic::Disconnected: name = "disconnected"; break;
    case NetworkDiagnostic::StartFailed: name = "start_failed"; break;
    case NetworkDiagnostic::ConnectFailed: name = "connect_failed"; break;
    case NetworkDiagnostic::Timeout: name = "timeout"; break;
    case NetworkDiagnostic::MissingAddress: name = "missing_address"; break;
    case NetworkDiagnostic::InvalidTiming: name = "invalid_timing"; break;
    }
    emit(std::string("network event=") + name + " connected=" + (update.connected ? "true" : "false") +
         " ip=" + (update.connected ? token(update.address) : "-"));
}
} // namespace esp32fan
