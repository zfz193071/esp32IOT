#include "adapters/NetworkHttpApi.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using namespace esp32fan;
static void check(bool ok, const char* rule) {
    if (!ok) { std::cerr << "FAIL: " << rule << '\n'; std::exit(1); }
}
struct Listener : HttpListener {
    bool open = false, fail = false, pending = false, active = false;
    unsigned begins = 0, ends = 0, accepts = 0, reads = 0, writes = 0;
    std::string address, input, output;
    std::size_t position = 0;
    std::vector<std::string> events;
    bool begin(const char* ip, std::uint16_t port) override {
        check(!open && !active && port == 80, "open only after prior listener/client cleanup");
        ++begins; events.push_back("begin"); address = ip;
        open = !fail; return open;
    }
    void end() override {
        check(!active, "HTTP client is discarded before listener end");
        ++ends; events.push_back("end"); open = pending = false;
    }
    void connect(const std::string& bytes) {
        check(open && !active && !pending, "client belongs to the current listener");
        input = bytes; output.clear(); position = 0; pending = true;
    }
    bool accept() override {
        check(open && !active, "poll only when listener is live"); ++accepts;
        if (!pending) return false;
        pending = false; active = true; return true;
    }
    HttpIoResult receive(char* bytes, std::size_t capacity) override {
        check(active && capacity <= 256, "receive bounded active client"); ++reads;
        const auto count = std::min(capacity, input.size() - position);
        if (!count) return {HttpIoStatus::WouldBlock, 0};
        std::memcpy(bytes, input.data() + position, count); position += count;
        return {HttpIoStatus::Progress, count};
    }
    HttpIoResult send(const char* bytes, std::size_t size) override {
        check(active && size <= 256, "send bounded active client"); ++writes;
        output.append(bytes, size); return {HttpIoStatus::Progress, size};
    }
    void close() override { check(active, "close active client once"); events.push_back("close"); active = false; }
};
static NetworkUpdate online(const std::string& ip = "192.168.1.10") {
    NetworkUpdate update; update.connected = true; update.address = ip; return update;
}
static std::string command(const char* host = "192.168.1.10") {
    return std::string("PUT /api/v1/power HTTP/1.1\r\nHost: ") + host + "\r\nOrigin: http://" + host +
        "\r\nContent-Type: application/json\r\nContent-Length: 11\r\n\r\n{\"on\":true}";
}
static void retryAndCleanup() {
    Listener wire;
    {
        NetworkHttpApi api(wire);
        check(wire.begins == 0 && wire.ends == 0, "construction has no network effects");
        api.update(0, NetworkUpdate{}); api.update(1, online("")); api.update(2, online(std::string(100, 'x')));
        check(wire.begins == 0 && wire.accepts == 0, "offline or missing/oversized IP never opens or polls");
        wire.fail = true;
        api.update(0xfffffff0u, online());
        check(wire.begins == 1 && !api.listening() && wire.events.back() == "end", "failed start cleaned immediately");
        api.update(0xfffffff1u, NetworkUpdate{});
        api.update(0xfffffff2u, online("192.168.1.11"));
        api.update(4983, online("192.168.1.11"));
        check(wire.begins == 1, "link/address flapping does not bypass retry delay across clock wrap");
        wire.fail = false;
        api.update(4984, online("192.168.1.11"));
        check(wire.begins == 2 && api.listening() && wire.accepts == 0 && wire.address == "192.168.1.11",
              "retry at exact deadline uses latest trusted address, without same-cycle accept");
        for (unsigned t = 4985; t < 5000; ++t) api.update(t, online("192.168.1.11"));
        check(wire.begins == 2 && wire.accepts == 15, "stable network polls without rebinding");
        api.update(5000, NetworkUpdate{});
        const auto ends = wire.ends;
        for (unsigned t = 5001; t < 5010; ++t) api.update(t, NetworkUpdate{});
        check(wire.ends == ends && wire.accepts == 15 && !api.listening(), "offline loop neither polls nor repeats shutdown");
    }
    check(!wire.open, "destruction leaves no listener");
}
static void discardOldWork() {
    for (unsigned phase = 0; phase < 4; ++phase) {
        Listener wire; NetworkHttpApi api(wire); WriteRequest request;
        api.update(0, online());
        wire.connect(phase == 0 ? "PUT /api/v1/" : command());
        api.update(1, online()); api.update(2, online());
        if (phase >= 2) check(api.takeRequest(request), "ready API request before network change");
        if (phase == 3) { StateResponse response; response.body = "old-response"; api.sendResponse(response); }
        wire.events.clear();
        // Exercise receive, ready, application-owned and queued response phases.
        api.update(3, phase % 2 ? online("192.168.1.11") : NetworkUpdate{});
        check(wire.events == std::vector<std::string>({"close", "end"}) && wire.writes == 0 &&
              !api.listening() && !api.takeRequest(request), "old connection discarded before admission or response send");
        api.sendResponse(StateResponse{});
        api.update(5000, online("192.168.1.11"));
        check(api.listening() && !api.takeRequest(request), "reopened listener never replays old request");
        wire.connect(command("192.168.1.11")); api.update(5001, online("192.168.1.11"));
        api.update(5002, online("192.168.1.11"));
        check(api.takeRequest(request) && request.host == "192.168.1.11", "only newly received command is delivered");
    }
}
struct Radio : NetworkPort {
    NetworkLink link = NetworkLink::Disconnected;
    std::string ip = "192.168.1.10";
    unsigned polls = 0;
    bool begin() override { link = NetworkLink::Connecting; return true; }
    NetworkLink status() override { ++polls; return link; }
    std::string address() override { return ip; }
    void disconnect() override { link = NetworkLink::Disconnected; }
};
struct Output : OutputPort {
    bool fan = true;
    void initializeOff() override { fan = false; }
    void setFan(bool on) override { fan = on; }
    void setIndicator(bool) override {}
};
struct Disk : ConfigPersistence {
    ReadResult read(ConfigRecord&) override { return ReadResult::Missing; }
    PersistenceResult replace(const ConfigRecord&) override { return PersistenceResult::Saved; }
};
struct Log : DiagnosticPort {
    void startup(LoadResult, const DeviceState&) override {}
    void stateChanged(const DeviceState&) override {}
    void temperature(TemperatureDiagnostic) override {}
    void network(const NetworkUpdate&) override {}
};
struct Sensor : TemperaturePort {
    unsigned starts = 0, polls = 0;
    bool start() override { ++starts; return true; }
    TemperaturePoll poll(double& value) override { ++polls; value = 25; return TemperaturePoll::Ready; }
    void cancel() override {}
};
static void appIntegration() {
    Listener wire; NetworkHttpApi api(wire); Radio radio; NetworkService network(radio);
    Output out; Disk disk; ConfigStore store(disk); Log log; Sensor sensor; TemperatureService temperature(sensor);
    AppLoop app("network-http", store, out, log, 20, &temperature, &network, &api);
    check(!app.step(0, false) && wire.begins == 0 && radio.polls == 0, "no network work before startup");
    app.start(); app.step(0, false);
    radio.link = NetworkLink::Connected;
    app.step(20, false);
    check(api.listening() && app.snapshot().wifiConnected && sensor.polls == 1, "one loop brings up trusted network and sampler");
    wire.connect(command()); app.step(21, false); app.step(22, false);
    check(out.fan && wire.output.empty(), "dynamic origin and listener share network result; output precedes response");
    radio.ip = "192.168.1.11"; app.step(23, false);
    check(!api.listening() && wire.output.empty() && out.fan, "IP change drops queued response without undoing executed command");
    app.step(30, true); app.step(50, true);
    check(!out.fan, "button works during listener retry delay");
    radio.link = NetworkLink::Disconnected; app.step(60, false);
    check(!app.snapshot().wifiConnected && !api.listening(), "same-cycle link loss removes HTTP service");
    app.step(1000, false); app.step(1001, false);
    check(sensor.starts == 2 && sensor.polls == 2, "temperature sampling continues while network is down");
    radio.link = NetworkLink::Connected;
    app.step(5019, false);
    check(!api.listening(), "reconnect retains listener retry budget");
    app.step(5020, false);
    wire.connect(command("192.168.1.11")); app.step(5021, false); app.step(5022, false);
    check(api.listening() && out.fan && wire.address == radio.ip, "new listener and trusted IP admit a fresh command after reconnect");
    // A dropped response must not undo a command already applied to the output.
    radio.link = NetworkLink::Disconnected; app.step(5023, false);
    check(!api.listening() && out.fan && wire.writes == 0, "second disconnect also drops the queued response");
    check(radio.polls == 15, "NetworkService is observed once per started AppLoop step");
}
int main() {
    retryAndCleanup(); discardOldWork(); appIntegration();
    std::cout << "PASS: network-managed HTTP lifecycle, retry pacing, stale-work cleanup and local control (host)\n";
}
