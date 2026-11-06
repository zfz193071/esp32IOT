#include "board/SocketHttpTransport.h"
#include "adapters/NetworkHttpApi.h"
#include "WebAssets.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using namespace esp32fan;
static void check(bool ok, const char* rule) {
    if (!ok) { std::cerr << "FAIL: " << rule << " (errno " << errno << ")\n"; std::exit(1); }
}
// POSIX-only test closer; production ESP32 uses the dedicated FreeRTOS worker.
struct Closer : SocketCloser {
    bool delayed = false, refuse = false;
    std::vector<int> pending;
    bool release(int fd) override {
        if (refuse) return false;
        if (delayed) { check(pending.size() < 2, "at most two retired descriptors"); pending.push_back(fd); }
        else check(::close(fd) == 0, "close owned socket once");
        return true;
    }
    bool idle() override { return pending.empty(); }
    void flush() { for (int fd : pending) check(::close(fd) == 0, "deferred close"); pending.clear(); }
    ~Closer() override { flush(); }
};
struct Client {
    int fd;
    explicit Client(std::uint16_t port) : fd(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) {
        check(fd >= 0, "client socket");
        sockaddr_in target{}; target.sin_family = AF_INET; target.sin_port = htons(port);
        check(::inet_pton(AF_INET, "127.0.0.1", &target.sin_addr) == 1, "loopback address");
        timeval timeout{}; timeout.tv_sec = 2;
        check(::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0, "client connect deadline");
#ifdef SO_NOSIGPIPE
        const int on = 1;
        check(::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)) == 0, "client SIGPIPE suppressed");
#endif
        check(::connect(fd, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == 0, "connect to test-owned listener");
        check(::fcntl(fd, F_SETFL, O_NONBLOCK) == 0, "nonblocking test client");
    }
    ~Client() { ::close(fd); }
};
static long sendBytes(int fd, const char* data, std::size_t size) {
#ifdef MSG_NOSIGNAL
    return ::send(fd, data, size, MSG_NOSIGNAL);
#else
    return ::send(fd, data, size, 0);
#endif
}
static void yieldNetwork() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
static void acceptClient(SocketHttpTransport& transport) {
    for (unsigned i = 0; i < 2000; ++i) { if (transport.accept()) return; yieldNetwork(); }
    check(false, "client accepted within test deadline");
}
static std::string get(const std::string& path, const std::string& method = "GET") {
    return method + " " + path + " HTTP/1.1\r\nHost: fan.test\r\n\r\n";
}
static std::string put(bool on, const char* origin = "http://fan.test") {
    const std::string body = on ? "{\"on\":true}" : "{\"on\":false}";
    return std::string("PUT /api/v1/power HTTP/1.1\r\nHost: fan.test\r\nOrigin: ") + origin +
        "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}
struct Output : OutputPort {
    bool fan = true;
    unsigned writes = 0;
    void initializeOff() override { fan = false; }
    void setFan(bool on) override { fan = on; ++writes; }
    void setIndicator(bool) override {}
};
struct Storage : ConfigPersistence {
    ReadResult read(ConfigRecord&) override { return ReadResult::Missing; }
    PersistenceResult replace(const ConfigRecord&) override { return PersistenceResult::Saved; }
};
struct Log : DiagnosticPort {
    void startup(LoadResult, const DeviceState&) override {}
    void stateChanged(const DeviceState&) override {}
    void temperature(TemperatureDiagnostic) override {}
    void network(const NetworkUpdate&) override {}
};
static WritePolicy policy() { WritePolicy p; p.allowedOrigins = {"http://fan.test"}; return p; }
struct Radio : NetworkPort {
    NetworkLink link = NetworkLink::Connected;
    bool begin() override { link = NetworkLink::Connecting; return true; }
    NetworkLink status() override { return link; }
    std::string address() override { return "127.0.0.1"; }
    void disconnect() override { link = NetworkLink::Disconnected; }
};
struct PressureSensor : TemperaturePort {
    unsigned starts = 0, polls = 0, failures = 0, samples = 0;
    bool start() override { ++starts; return true; }
    TemperaturePoll poll(double& value) override {
        ++polls;
        if (polls % 7 == 0) { ++failures; return TemperaturePoll::Invalid; }
        ++samples;
        value = 25;
        return TemperaturePoll::Ready;
    }
    void cancel() override { check(false, "prompt pressure sensor must not time out"); }
};
static TemperatureTiming pressureTiming() {
    TemperatureTiming timing;
    timing.intervalMs = 10; timing.timeoutMs = 10;
    return timing; // Accelerated simulated timing, not device configuration.
}
struct Server {
    Closer closer;
    SocketHttpTransport transport{closer};
    NetworkHttpApi http{transport, 0};
    Radio radio;
    NetworkService network{radio};
    Output output;
    Storage disk;
    ConfigStore store{disk};
    Log log;
    AppLoop app;
    bool pressureInputs = false;
    std::uint32_t time = 0;
    Server(TemperatureService* temperature = nullptr)
        : app("tcp-test", store, output, log, 20, temperature, &network, &http, policy()) {
        app.start();
        step();
        check(http.listening() && transport.port() != 0, "network-managed ephemeral loopback listener (requires local networking permission)");
    }
    void step(bool pressed = false) {
        if (pressureInputs) pressed = time % 100 >= 30 && time % 100 < 60;
        app.step(time++, pressed);
    }
    std::string exchange(const std::string& request, std::size_t fragment = 7, bool halfClose = false) {
        Client client(transport.port());
        std::size_t offset = 0;
        std::string response;
        bool shut = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            if (offset < request.size()) {
                const auto count = sendBytes(client.fd, request.data() + offset, std::min(fragment, request.size() - offset));
                if (count > 0) offset += static_cast<std::size_t>(count);
                else check(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR), "fragment send");
            } else if (halfClose && !shut) { check(::shutdown(client.fd, SHUT_WR) == 0, "peer half close"); shut = true; }
            step();
            char bytes[181];
            const auto count = ::recv(client.fd, bytes, sizeof(bytes), 0);
            if (count > 0) response.append(bytes, static_cast<std::size_t>(count));
            else if (count == 0) return response;
            else check(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR, "receive response without reset");
            yieldNetwork();
        }
        check(false, "HTTP exchange completes within wall-clock deadline");
        return response;
    }
};
static std::string body(const std::string& response) {
    const auto end = response.find("\r\n\r\n");
    check(end != std::string::npos, "complete HTTP headers");
    check(response.find("Cache-Control: no-store\r\n") != std::string::npos &&
          response.find("Connection: close\r\n") != std::string::npos, "wire response headers");
    return response.substr(end + 4);
}
static void httpIntegration() {
    Server server;
    for (std::size_t i = 0; i < web::assetCount; ++i) {
        const auto& asset = web::assets[i];
        const auto response = server.exchange(get(asset.path), 1);
        check(response.find("HTTP/1.1 200 OK\r\n") == 0, "static GET over real TCP");
        check(response.find("Content-Length: " + std::to_string(asset.size) + "\r\n") != std::string::npos,
              "real TCP content length");
        check(body(response) == std::string(reinterpret_cast<const char*>(asset.data), asset.size),
              "every generated asset arrives byte-identical over TCP");
    }
    check(body(server.exchange(get("/", "HEAD"))).empty(), "HEAD over TCP has no body");
    const auto on = server.exchange(put(true));
    check(on.find("HTTP/1.1 200") == 0 && server.output.fan && body(on).find("\"fanOutputOn\":true") != std::string::npos,
          "real socket/parser/AppLoop write controls output and returns snapshot");
    const auto revision = server.app.snapshot().revision;
    check(server.exchange(put(false, "http://foreign.test")).find("HTTP/1.1 403") == 0 &&
          server.output.fan && server.app.snapshot().revision == revision, "foreign origin rejected through TCP");
    check(body(server.exchange(get("/api/v1/state"))).find("\"fanOutputOn\":true") != std::string::npos &&
          server.app.snapshot().revision == revision, "TCP state read has no side effects");
    check(server.exchange("PUT /api/v1/power HTTP/1.1\r\nHost: fan.test\r\nContent-Length: 11\r\n\r\n{", 64, true)
          .find("HTTP/1.1 400") == 0 && server.output.fan, "truncated peer half close never executes a write");
    // Idle client occupies the HTTP slot, but the physical-input schedule runs.
    Client idle(server.transport.port());
    for (unsigned i = 0; i < 25; ++i) server.step(false);
    for (unsigned i = 0; i < 25; ++i) server.step(true);
    check(!server.output.fan, "local button progresses with an idle TCP client");
    server.time += 2000;
    for (unsigned i = 0; i < 10; ++i) server.step(false);
    check(server.exchange(get("/api/v1/state")).find("HTTP/1.1 200") == 0, "new client works after receive timeout");
    server.radio.link = NetworkLink::Disconnected; server.step();
    check(server.transport.port() == 0 && !server.http.listening(), "network loss automatically stops the listener");
    server.time += 5000;
    server.radio.link = NetworkLink::Connected; server.step();
    check(server.http.listening() && server.transport.port() != 0, "same-IP reconnect automatically reopens listener");
    check(server.exchange(get("/api/v1/state")).find("HTTP/1.1 200") == 0, "HTTP works after listener restart");
}
static void disconnectDuringWrite() {
    for (bool complete : {false, true}) {
        Server server;
        Client oldClient(server.transport.port());
        const auto request = put(true);
        const auto size = request.size() - (complete ? 0 : 1);
        check(sendBytes(oldClient.fd, request.data(), size) == static_cast<long>(size),
              "queue small write request before simulated link loss");
        // For a complete request, stop immediately after application: no sending step yet.
        for (unsigned step = 0; step < 30 && !server.output.fan; ++step) {
            server.step();
            yieldNetwork();
        }
        check(server.output.fan == complete && server.output.writes == (complete ? 1u : 0u),
              "only a complete write may apply before link loss");
        server.radio.link = NetworkLink::Disconnected;
        server.step();
        check(!server.http.listening() && server.transport.port() == 0 && server.output.fan == complete,
              "link loss retires HTTP without reverting an applied output");
        bool closed = false;
        for (unsigned attempt = 0; attempt < 2000; ++attempt) {
            char bytes[256];
            const auto count = ::recv(oldClient.fd, bytes, sizeof(bytes), 0);
            check(count <= 0, "no success response escapes after link loss before send");
            if (count == 0 || (count < 0 && errno == ECONNRESET)) { closed = true; break; }
            check(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR,
                  "old connection closes or reports reset");
            yieldNetwork();
        }
        check(closed, "old connection is retired within wall-clock deadline");
        server.time += 5000;
        server.radio.link = NetworkLink::Connected;
        server.step();
        check(server.http.listening(), "listener recovers after interrupted write");
        const auto snapshot = server.exchange(get("/api/v1/state"));
        check(snapshot.find("HTTP/1.1 200 OK\r\n") == 0 && body(snapshot).find(
                  complete ? "\"fanOutputOn\":true" : "\"fanOutputOn\":false") != std::string::npos,
              "new connection can query the actual outcome of interrupted write");
        check(server.output.writes == (complete ? 1u : 0u),
              "reconnection and outcome query never replay the previous command");
        // A newly issued opposite command proves parser/request state is fresh.
        const auto fresh = server.exchange(put(!complete));
        check(fresh.find("HTTP/1.1 200 OK\r\n") == 0 && server.output.fan == !complete &&
                  server.output.writes == (complete ? 2u : 1u),
              "fresh write after reconnect works without an old request prefix");
    }
    std::cout << "PASS: interrupted TCP writes preserve actual outcome without replay after reconnect\n";
}

static void repeatedHttpPressure() {
    PressureSensor sensor;
    TemperatureService temperature(sensor, pressureTiming());
    Server loaded(&temperature);
    loaded.pressureInputs = true;
    for (unsigned request = 0; request < 100; ++request) {
        // Alternate bytewise and larger fragments; each request opens a new TCP connection.
        const auto response = loaded.exchange(get("/api/v1/state"), request % 2 ? 7 : 1);
        check(response.find("HTTP/1.1 200 OK\r\n") == 0 &&
              body(response).find("\"bootId\":\"tcp-test\"") != std::string::npos,
              "100 consecutive TCP requests return complete state responses");
    }
    check(loaded.output.writes >= 20 && sensor.failures > 0 && sensor.samples > 0,
          "TCP pressure overlaps at least 20 button events and sensor failure/recovery");

    // Replay exactly the same simulated input/time sequence without HTTP clients.
    PressureSensor baselineSensor;
    TemperatureService baselineTemperature(baselineSensor, pressureTiming());
    Server baseline(&baselineTemperature);
    baseline.pressureInputs = true;
    while (baseline.time < loaded.time) baseline.step();
    const auto actual = loaded.app.snapshot();
    const auto expected = baseline.app.snapshot();
    check(loaded.output.writes == baseline.output.writes && loaded.output.fan == baseline.output.fan &&
          actual.lastCommandSource == CommandSource::Button && actual.revision == expected.revision,
          "HTTP reads neither add output changes nor suppress local button commands");
    check(sensor.starts == baselineSensor.starts && sensor.polls == baselineSensor.polls &&
          sensor.failures == baselineSensor.failures && sensor.samples == baselineSensor.samples &&
          actual.sensorStatus == expected.sensorStatus && actual.sampleAgeMs == expected.sampleAgeMs,
          "sampling and recovery match idle baseline at identical simulated times");
    std::cout << "PASS: 100 real TCP state requests preserve button and sampling progress (simulated inputs/time)\n";
}

static void transportLifecycle() {
    Closer closer; SocketHttpTransport transport(closer);
    check(!transport.begin("0.0.0.0", 0) && !transport.begin("localhost", 0) &&
          !transport.begin("224.0.0.1", 0) && !transport.begin(nullptr, 0), "refuse wildcard, DNS, multicast and null address");
    check(transport.begin("127.0.0.1", 0) && transport.port() != 0, "listener obtains actual ephemeral port");
    const auto port = transport.port();
    check(!transport.begin("127.0.0.1", 0) && transport.port() == port, "begin cannot replace a live listener");
    Closer secondCloser; SocketHttpTransport second(secondCloser);
    check(!second.begin("127.0.0.1", port) && second.port() == 0 && second.lastError() != 0,
          "occupied port fails and releases its candidate descriptor");
    check(!transport.accept(), "empty nonblocking listener returns immediately");
    Client client(port); acceptClient(transport);
    check(!transport.accept(), "never overwrite active client descriptor");
    char bytes[1024];
    check(transport.receive(bytes, sizeof(bytes)).status == HttpIoStatus::WouldBlock, "idle accepted client is nonblocking");
    const std::string data(1024, 'x');
    check(sendBytes(client.fd, data.data(), data.size()) == 1024, "queue client data");
    HttpIoResult received{HttpIoStatus::WouldBlock, 0};
    for (unsigned i = 0; i < 2000 && received.status == HttpIoStatus::WouldBlock; ++i) {
        received = transport.receive(bytes, sizeof(bytes)); yieldNetwork();
    }
    check(received.status == HttpIoStatus::Progress && received.size <= 256, "driver itself caps each receive");
    const auto sent = transport.send(data.data(), data.size());
    check(sent.status == HttpIoStatus::Progress && sent.size <= 256, "driver itself caps each send");
    closer.refuse = true; transport.close();
    check(!transport.accept() && transport.receive(bytes, sizeof(bytes)).status == HttpIoStatus::Error,
          "refused retirement retains ownership and disables old client I/O");
    closer.refuse = false; closer.delayed = true; transport.close();
    check(closer.pending.size() == 1 && !transport.accept(), "deferred close gates next accept");
    transport.end(); transport.end();
    check(closer.pending.size() == 2 && !transport.begin("127.0.0.1", 0), "at most listener and client await closure, without double close");
    closer.flush(); closer.delayed = false;
    check(transport.begin("127.0.0.1", 0), "listener restarts only after retired handles are reclaimed");
}
int main() {
    transportLifecycle(); httpIntegration(); repeatedHttpPressure(); disconnectDuringWrite();
    std::cout << "PASS: real loopback TCP transport, embedded assets, API, local control and listener lifecycle (host only)\n";
}
