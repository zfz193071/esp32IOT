#include "adapters/TextDiagnostics.h"
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace esp32fan;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
class Sink : public DiagnosticSink {
public:
    bool accept = true;
    unsigned calls = 0;
    std::vector<std::string> lines;
    bool tryWriteLine(const std::string& line) override {
        ++calls;
        if (!accept) return false;
        lines.push_back(line);
        return true;
    }
};
int main() {
    Sink sink;
    TextDiagnostics diagnostics(sink);
    DeviceState state;
    state.bootId = "boot-1";
    const LoadResult results[] = {LoadResult::Loaded, LoadResult::MissingDefaults,
        LoadResult::CorruptDefaults, LoadResult::VersionDefaults, LoadResult::ReadFailureDefaults};
    const char* names[] = {"loaded", "missing_defaults", "corrupt_defaults", "version_defaults", "read_failure_defaults"};
    for (unsigned i = 0; i < 5; ++i) {
        diagnostics.startup(results[i], state);
        check(sink.lines.back() == std::string("startup config=") + names[i] +
            " boot=boot-1 revision=0 mode=manual output=off source=system fault=none on_c=30 off_c=28 config_save_blocked=false",
            "startup identifies configuration fallback and safe initial state");
    }
    state.mode = Mode::Auto;
    state.fanOutputOn = true;
    state.lastCommandSource = CommandSource::Web;
    state.revision = 42;
    diagnostics.stateChanged(state);
    check(sink.lines.back().find("revision=42 mode=auto output=on source=web") != std::string::npos,
          "state reports control output and command source");
    state.mode = Mode::Manual;
    state.fanOutputOn = false;
    state.fault = Fault::TemperatureUnavailable;
    diagnostics.stateChanged(state);
    check(sink.lines.back().find("fault=temperature_unavailable") != std::string::npos, "fault is visible");
    state.configSaveBlocked = true;
    diagnostics.stateChanged(state);
    check(sink.lines.back().find("config_save_blocked=true") != std::string::npos,
          "uncertain save status is visible in diagnostics");
    const auto count = sink.calls;
    diagnostics.temperature(TemperatureDiagnostic::None);
    diagnostics.network(NetworkUpdate{});
    check(sink.calls == count, "no event produces no output");
    diagnostics.temperature(TemperatureDiagnostic::Timeout);
    check(sink.lines.back() == "temperature event=timeout", "conversion timeout has a stable name");
    NetworkUpdate update;
    update.connected = true;
    update.address = "192.168.1.2";
    update.diagnostic = NetworkDiagnostic::Connected;
    diagnostics.network(update);
    check(sink.lines.back() == "network event=connected connected=true ip=192.168.1.2", "ready IP is logged");
    update.connected = false;
    update.diagnostic = NetworkDiagnostic::Disconnected;
    diagnostics.network(update);
    check(sink.lines.back() == "network event=disconnected connected=false ip=-", "disconnect does not advertise stale IP");
    state.bootId = "bad\r\nboot id=" + std::string(300, 'x');
    diagnostics.stateChanged(state);
    check(sink.lines.back().find_first_of("\r\n") == std::string::npos && sink.lines.back().size() < 400,
          "identifiers cannot inject log lines or grow without bound");
    sink.accept = false;
    const auto before = sink.calls;
    diagnostics.stateChanged(state);
    check(sink.calls == before + 1 && diagnostics.droppedLines() == 1, "busy sink is attempted once without retry");
    sink.accept = true;
    diagnostics.temperature(TemperatureDiagnostic::ReadFailed);
    check(sink.lines.back() == "temperature event=read_failed" && diagnostics.droppedLines() == 1,
          "logging resumes after sink recovers without replaying dropped lines");
    std::cout << "PASS: diagnostic text, fallback reasons and bounded sink behavior (host only)\n";
}
