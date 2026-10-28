#include "app/AppLoop.h"
#include <cstdlib>
#include <deque>
#include <iostream>
#include <vector>

using namespace esp32fan;
void check(bool ok, const char* rule) {
    if (!ok) { std::cerr << "FAIL: " << rule << '\n'; std::exit(EXIT_FAILURE); }
}
struct Outputs : OutputPort {
    bool ready = false, fan = true, led = true;
    unsigned initializes = 0;
    std::vector<bool> writes;
    void initializeOff() override { ready = true; fan = false; ++initializes; }
    void setFan(bool on) override { check(ready, "output initialized before write"); fan = on; writes.push_back(on); }
    void setIndicator(bool on) override { led = on; }
};
struct Storage : ConfigPersistence {
    Outputs& out;
    unsigned loads = 0;
    explicit Storage(Outputs& output) : out(output) {}
    ReadResult read(ConfigRecord& record) override {
        check(out.ready && !out.fan && !out.led, "hardware off and LED off precede storage access");
        ++loads;
        record.thresholds.onThresholdC = 32;
        record.thresholds.offThresholdC = 30;
        return ReadResult::Found;
    }
    bool replaceAtomically(const ConfigRecord&) override { return true; }
};
struct Diagnostics : DiagnosticPort {
    unsigned boots = 0, changes = 0, networkEvents = 0, temperatureEvents = 0;
    void startup(LoadResult result, const DeviceState& state) override {
        check(result == LoadResult::Loaded && !state.fanOutputOn, "startup reports loaded configuration and off state");
        ++boots;
    }
    void stateChanged(const DeviceState&) override { ++changes; }
    void temperature(TemperatureDiagnostic) override { ++temperatureEvents; }
    void network(const NetworkUpdate&) override { ++networkEvents; }
};
struct Api : ApiPort {
    Outputs& out;
    std::deque<WriteRequest> requests;
    std::vector<StateResponse> responses;
    unsigned polls = 0;
    explicit Api(Outputs& output) : out(output) {}
    bool takeRequest(WriteRequest& request) override {
        ++polls;
        if (requests.empty()) return false;
        request = requests.front(); requests.pop_front(); return true;
    }
    void sendResponse(const StateResponse& response) override {
        if (response.status == 200) {
            const auto expected = out.fan ? "\"fanOutputOn\":true" : "\"fanOutputOn\":false";
            check(response.body.find(expected) != std::string::npos && out.fan == out.led,
                  "response sent only after final output and LED submission");
        }
        responses.push_back(response);
    }
};
struct Temperature : TemperaturePort {
    unsigned starts = 0, polls = 0;
    TemperaturePoll next = TemperaturePoll::Pending;
    double value = 40;
    bool start() override { ++starts; next = TemperaturePoll::Pending; return true; }
    TemperaturePoll poll(double& result) override { ++polls; result = value; return next; }
    void cancel() override { next = TemperaturePoll::Pending; }
};
struct Network : NetworkPort {
    NetworkLink link = NetworkLink::Disconnected;
    std::string ip = "192.168.1.20";
    bool begin() override { link = NetworkLink::Connecting; return true; }
    NetworkLink status() override { return link; }
    std::string address() override { return ip; }
    void disconnect() override { link = NetworkLink::Disconnected; }
};
struct Infrared : InfraredPort {
    std::deque<InfraredFrame> frames;
    bool takeFrame(InfraredFrame& frame) override {
        if (frames.empty()) return false;
        frame = frames.front(); frames.pop_front(); return true;
    }
    void press(std::uint32_t now, bool repeat = false) {
        InfraredFrame frame;
        frame.protocol = 7; frame.address = 42; frame.command = 9;
        frame.receivedAtMs = now; frame.valid = true; frame.repeat = repeat;
        frames.push_back(frame);
    }
};
WriteRequest request(const std::string& path, const std::string& body, const std::string& ip = "192.168.1.20") {
    WriteRequest req;
    req.method = "PUT"; req.path = path; req.body = body;
    req.host = ip; req.hasOrigin = true; req.origin = "http://" + ip;
    req.contentType = "application/json";
    return req;
}
int main() {
    Outputs output;
    Storage persistence(output);
    ConfigStore store(persistence);
    Diagnostics log;
    Api api(output);
    Temperature sensor;
    TemperatureService temperature(sensor);
    Network radio;
    NetworkService network(radio);
    AppLoop app("app-test", store, output, log, 20, &temperature, &network, &api);
    check(!app.step(0, false) && api.polls == 0 && sensor.starts == 0, "step before start has no side effects");
    app.start(); app.start();
    check(output.initializes == 1 && persistence.loads == 1 && log.boots == 1 &&
              app.snapshot().onThresholdC == 32 && app.snapshot().mode == Mode::Manual,
          "startup once restores thresholds only after off initialization");
    app.step(0, false);
    radio.link = NetworkLink::Connected;
    sensor.next = TemperaturePoll::Ready;
    app.step(20, false);
    check(app.snapshot().hasTemperature && app.snapshot().wifiConnected,
          "loop assembles temperature and network state before accepting commands");
    app.step(30, true);
    api.requests.push_back(request("/api/v1/power", "{\"on\":false}"));
    app.step(50, true);
    check(!output.fan && output.writes.empty() && app.snapshot().lastCommandSource == CommandSource::Web,
          "button-on then API-off in one loop emits only final off, without on pulse");
    api.requests.push_back(request("/api/v1/mode", "{\"mode\":\"auto\"}"));
    app.step(60, false);
    check(output.fan && output.led && app.snapshot().mode == Mode::Auto, "API auto reaches physical-output seam");
    app.step(80, false);
    const auto logged = log.changes;
    for (unsigned t = 81; t < 100; ++t) app.step(t, false);
    check(log.changes == logged && output.writes.size() == 1, "idle loop does not spam output or state logs");
    radio.ip = "192.168.1.21";
    api.requests.push_back(request("/api/v1/power", "{\"on\":false}"));
    app.step(100, false);
    check(api.responses.back().status == 403 && output.fan, "old dynamic IP loses write authority immediately");
    api.requests.push_back(request("/api/v1/power", "{\"on\":false}", radio.ip));
    app.step(101, false);
    check(api.responses.back().status == 200 && !output.fan, "new IP is trusted from network driver, not incoming Host");
    api.requests.push_back(request("/api/v1/mode", "{\"mode\":\"auto\"}", radio.ip));
    app.step(102, false);
    check(output.fan, "auto resumed explicitly for health-order test");
    app.step(1000, false); // begin new conversion
    sensor.next = TemperaturePoll::Invalid;
    api.requests.push_back(request("/api/v1/mode", "{\"mode\":\"auto\"}", radio.ip));
    app.step(1001, false);
    check(api.responses.back().status == 409 && !output.fan && !output.led &&
              app.snapshot().fault == Fault::TemperatureUnavailable,
          "health invalidation precedes auto admission and final off submission");
    api.requests.push_back(request("/api/v1/power", "{\"on\":true}", radio.ip));
    app.step(1002, false);
    check(output.fan && app.snapshot().mode == Mode::Manual, "manual command remains possible after temperature fault");
    radio.link = NetworkLink::Disconnected;
    api.requests.push_back(request("/api/v1/power", "{\"on\":false}", radio.ip));
    app.step(1003, false);
    check(api.responses.back().status == 403 && output.fan && !app.snapshot().wifiConnected,
          "disconnect removes dynamic authority but does not switch local output");

    // Continuous reads: one request per cycle, with input and conversion still scheduled.
    WriteRequest get;
    get.method = "GET"; get.path = "/api/v1/state";
    for (int i = 0; i < 100; ++i) api.requests.push_back(get);
    const auto responses = api.responses.size();
    app.step(1100, true);
    app.step(1120, true);
    check(!output.fan && api.responses.size() == responses + 2 && api.requests.size() == 98,
          "request flood is budgeted and does not starve debounced button");
    app.step(2000, false);
    sensor.next = TemperaturePoll::Ready;
    app.step(2020, false);
    check(app.snapshot().sensorStatus == SensorStatus::Ok && !output.fan &&
              app.snapshot().mode == Mode::Manual && api.requests.size() == 96,
          "temperature recovery progresses under API load without automatic restart");

    Outputs localOut;
    Storage localDisk(localOut);
    ConfigStore localStore(localDisk);
    Diagnostics localLog;
    AppLoop local("local-only", localStore, localOut, localLog, 20);
    local.start(); local.step(0, false); local.step(20, false);
    local.step(30, true); local.step(50, true);
    check(localOut.fan && localOut.led && !local.snapshot().wifiConnected,
          "local-only assembly requires no network, temperature or API ports");
    Outputs irOutput;
    Storage irDisk(irOutput);
    ConfigStore irStore(irDisk);
    Diagnostics irLog;
    Api irApi(irOutput);
    Infrared remote;
    InfraredBinding toggle;
    toggle.protocol = 7; toggle.address = 42; toggle.command = 9; toggle.action = InfraredAction::Toggle;
    InfraredInput irInput(remote, {toggle});
    WritePolicy trusted;
    trusted.allowedOrigins = {"http://192.168.1.20"};
    AppLoop irApp("infrared-app", irStore, irOutput, irLog, 20, nullptr, nullptr, &irApi, trusted, &irInput);
    irApp.start(); irApp.step(0, false); irApp.step(20, false);
    remote.press(30); irApp.step(30, false);
    check(irOutput.fan && irApp.snapshot().lastCommandSource == CommandSource::Infrared,
          "infrared toggle uses the unified controller and records source");
    irApp.step(40, true); irApp.step(60, true);
    check(!irOutput.fan && irApp.snapshot().lastCommandSource == CommandSource::Button,
          "physical button can take over an infrared output");
    remote.press(100, true); irApp.step(100, true);
    check(!irOutput.fan && irApp.snapshot().lastCommandSource == CommandSource::Button,
          "held IR repeat cannot undo a later manual button command");
    remote.press(350);
    irApi.requests.push_back(request("/api/v1/power", "{\"on\":false}"));
    const auto irWrites = irOutput.writes.size();
    irApp.step(350, false);
    check(!irOutput.fan && irOutput.writes.size() == irWrites &&
              irApp.snapshot().lastCommandSource == CommandSource::Web,
          "same-cycle IR then API applies final output only, with no transient on pulse");
    std::cout << "PASS: AppLoop startup, bounded scheduling, final outputs and API integration (host only)\n";
}
