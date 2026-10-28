#include "app/AppLoop.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace esp32fan;

void check(bool condition, const char* rule) {
    if (!condition) { std::cerr << "FAIL: " << rule << '\n'; std::exit(EXIT_FAILURE); }
}

struct Storage : ConfigPersistence {
    ReadResult read(ConfigRecord&) override { return ReadResult::Missing; }
    bool replaceAtomically(const ConfigRecord&) override { return true; }
};
struct Output : OutputPort {
    bool fan = true;
    bool led = true;
    unsigned writes = 0;
    void initializeOff() override { fan = false; }
    void setFan(bool on) override { fan = on; ++writes; }
    void setIndicator(bool on) override { led = on; }
};
struct Diagnostics : DiagnosticPort {
    unsigned invalid = 0;
    void startup(LoadResult, const DeviceState&) override {}
    void stateChanged(const DeviceState&) override {}
    void temperature(TemperatureDiagnostic diagnostic) override {
        if (diagnostic == TemperatureDiagnostic::ReadFailed) ++invalid;
    }
    void network(const NetworkUpdate&) override {}
};
struct Sensor : TemperaturePort {
    unsigned starts = 0;
    unsigned polls = 0;
    unsigned samples = 0;
    bool start() override { ++starts; return true; }
    TemperaturePoll poll(double& value) override {
        ++polls;
        if (polls == 6) return TemperaturePoll::Invalid;
        value = 25;
        ++samples;
        return TemperaturePoll::Ready;
    }
    void cancel() override {}
};
struct Network : NetworkPort {
    std::uint32_t now = 0;
    unsigned starts = 0;
    unsigned disconnects = 0;
    NetworkLink link = NetworkLink::Disconnected;
    bool begin() override { ++starts; link = NetworkLink::Connecting; return true; }
    NetworkLink status() override {
        if (starts && now >= 10 && now < 5000) return NetworkLink::Connected;
        if (now >= 5000 && now < 10000) return NetworkLink::Disconnected;
        if (starts >= 2 && now >= 10010) return NetworkLink::Connected;
        return link;
    }
    std::string address() override { return "192.168.1.20"; }
    void disconnect() override { ++disconnects; link = NetworkLink::Disconnected; }
};
struct Infrared : InfraredPort {
    std::uint32_t now = 0;
    unsigned frames = 0;
    bool takeFrame(InfraredFrame& frame) override {
        if (now < 100 || now > 3100 || now % 100 != 0) return false;
        frame.protocol = 7;
        frame.address = 42;
        frame.command = 9;
        frame.receivedAtMs = now;
        frame.valid = true;
        frame.repeat = now != 100;
        ++frames;
        return true;
    }
};
struct Api : ApiPort {
    Output& output;
    unsigned polls = 0;
    unsigned responses = 0;
    explicit Api(Output& out) : output(out) {}
    bool takeRequest(WriteRequest& request) override {
        ++polls;
        request.method = "GET";
        request.path = "/api/v1/state";
        return true; // A complete request remains available on every cycle.
    }
    void sendResponse(const StateResponse& response) override {
        ++responses;
        const auto field = output.fan ? "\"fanOutputOn\":true" : "\"fanOutputOn\":false";
        check(response.status == 200 && response.body.find(field) != std::string::npos &&
                  output.fan == output.led,
              "every flooded GET sees the final submitted fan and LED state");
    }
};

int main() {
    Storage persistence;
    ConfigStore store(persistence);
    Output output;
    Diagnostics diagnostics;
    Sensor sensor;
    TemperatureService temperature(sensor);
    Network networkPort;
    NetworkService network(networkPort);
    Infrared infraredPort;
    InfraredBinding toggle;
    toggle.protocol = 7; toggle.address = 42; toggle.command = 9;
    toggle.action = InfraredAction::Toggle;
    InfraredInput infrared(infraredPort, {toggle});
    Api api(output);
    AppLoop app("pressure", store, output, diagnostics, 20,
                &temperature, &network, &api, WritePolicy{}, &infrared);
    app.start();
    check(!output.fan && !output.led, "pressure run starts with both outputs off");

    for (std::uint32_t t = 0; t < 20000; ++t) {
        infraredPort.now = t;
        networkPort.now = t;
        const bool pressed = t % 400 >= 50 && t % 400 < 80;
        const auto writesBeforeNetworkEvent = output.writes;
        check(app.step(t, pressed), "pressure cycle remains active");
        if (t == 10 || t == 5000 || t == 10010)
            check(output.writes == writesBeforeNetworkEvent,
                  "network connect, disconnect and recovery do not switch the output");
        if (t == 10) check(app.snapshot().wifiConnected, "initial network connection is observed");
        if (t == 5000 || t == 9999)
            check(!app.snapshot().wifiConnected, "network failure clears connected state during API flood");
        if (t == 10010) check(app.snapshot().wifiConnected, "paced network retry restores connection");
        if (t == 5001)
            check(app.snapshot().sensorStatus == SensorStatus::Invalid,
                  "sensor failure is observed during continuous API and local input");
        if (t == 6001)
            check(app.snapshot().sensorStatus == SensorStatus::Ok,
                  "sensor recovers while API flood continues");
    }
    check(api.polls == 20000 && api.responses == 20000,
          "one response per cycle while requests never stop");
    check(sensor.starts == 20 && sensor.polls == 20 && sensor.samples == 19 && diagnostics.invalid == 1,
          "temperature attempts advance on schedule despite API flood and one failure");
    check(networkPort.starts == 2 && networkPort.disconnects == 1,
          "network retry waits five seconds after disconnection without starving local work");
    check(infraredPort.frames == 31 && output.writes == 51 && output.fan && output.led &&
              app.snapshot().lastCommandSource == CommandSource::Button,
          "50 debounced button presses and one infrared hold each act exactly once");
    check(app.snapshot().sampleAgeMs == 998 && app.snapshot().sensorStatus == SensorStatus::Ok,
          "last sample stays fresh after 20,000 simulated one-millisecond cycles");
    std::cout << "PASS: continuous API, button, infrared and sensor scheduling (host simulation only)\n";
}
