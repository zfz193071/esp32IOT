#include "adapters/HttpApiPort.h"
#include "WebAssets.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace esp32fan;
void check(bool ok, const char* rule) {
    if (!ok) { std::cerr << "FAIL: " << rule << '\n'; std::exit(EXIT_FAILURE); }
}
struct Wire : HttpTransport {
    bool pending = false, active = false, stall = false, eof = false, broken = false;
    std::string input, output;
    std::size_t position = 0, readChunk = 256, writeChunk = 256;
    unsigned accepts = 0, reads = 0, writes = 0, closes = 0;
    void connect(const std::string& bytes) {
        check(!active && !pending, "only one test client at a time");
        input = bytes; output.clear(); position = 0; pending = true;
        stall = eof = broken = false;
    }
    bool accept() override {
        check(!active, "never accept while an active client owns the response");
        ++accepts;
        if (!pending) return false;
        pending = false; active = true; return true;
    }
    HttpIoResult receive(char* bytes, std::size_t capacity) override {
        check(active && capacity <= 256, "receive uses a fixed budget"); ++reads;
        if (broken) return {HttpIoStatus::Error, 0};
        const auto size = std::min(std::min(capacity, readChunk), input.size() - position);
        if (size == 0) return {eof ? HttpIoStatus::End : HttpIoStatus::WouldBlock, 0};
        std::memcpy(bytes, input.data() + position, size); position += size;
        return {HttpIoStatus::Progress, size};
    }
    HttpIoResult send(const char* bytes, std::size_t size) override {
        check(active && size > 0 && size <= 256, "send uses a fixed budget"); ++writes;
        if (broken) return {HttpIoStatus::Error, 0};
        if (stall) return {HttpIoStatus::WouldBlock, 0};
        const auto sent = std::min(size, writeChunk);
        output.append(bytes, sent); return {HttpIoStatus::Progress, sent};
    }
    void close() override { check(active, "close each accepted client once"); active = false; ++closes; }
};
std::string get(const std::string& path, const std::string& method = "GET") {
    return method + " " + path + " HTTP/1.1\r\nHost: fan.local\r\n\r\n";
}
std::string put(const std::string& path, const std::string& body) {
    return "PUT " + path + " HTTP/1.1\r\nHost: fan.local\r\nOrigin: http://fan.local\r\n"
        "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\n\r\n" + body;
}
void poll(HttpApiPort& api, Wire& wire, std::uint32_t now) {
    const auto reads = wire.reads, writes = wire.writes, accepts = wire.accepts;
    api.poll(now);
    check(wire.reads - reads + wire.writes - writes + wire.accepts - accepts <= 1,
          "one accept, receive or send operation per main-loop cycle");
}
void drain(HttpApiPort& api, Wire& wire, std::uint32_t start = 10) {
    for (unsigned i = 0; i < 4000 && wire.active; ++i) poll(api, wire, start + i);
    check(!wire.active, "response finishes within bounded test cycles");
}
std::string body(const Wire& wire) {
    const auto end = wire.output.find("\r\n\r\n");
    check(end != std::string::npos, "response has complete headers");
    check(wire.output.find("Connection: close\r\n") != std::string::npos &&
          wire.output.find("Cache-Control: no-store\r\n") != std::string::npos,
          "every response closes and prevents caching");
    return wire.output.substr(end + 4);
}
void assetsAndRouting() {
    Wire wire; HttpApiPort api(wire);
    wire.readChunk = 1; wire.writeChunk = 17;
    for (std::size_t i = 0; i < web::assetCount; ++i) {
        const auto& asset = web::assets[i];
        wire.connect(get(std::string(asset.path) + "?v=1"));
        poll(api, wire, 0); drain(api, wire);
        check(wire.output.find("HTTP/1.1 200 OK\r\n") == 0, "static GET succeeds");
        check(body(wire) == std::string(reinterpret_cast<const char*>(asset.data), asset.size),
              "short writes preserve all embedded asset bytes");
        check(wire.output.find("Content-Length: " + std::to_string(asset.size) + "\r\n") != std::string::npos,
              "Content-Length is byte length, without a trailing NUL");
        wire.connect(get(asset.path, "HEAD")); poll(api, wire, 0); drain(api, wire);
        check(body(wire).empty() && wire.output.find("Content-Length: " + std::to_string(asset.size)) != std::string::npos,
              "HEAD sends GET length but no body");
    }
    wire.connect(get("/../index.html")); poll(api, wire, 0); drain(api, wire);
    check(wire.output.find("HTTP/1.1 404") == 0, "only exact allowlisted resources are served");
    wire.connect(get("/", "POST")); poll(api, wire, 0); drain(api, wire);
    check(wire.output.find("HTTP/1.1 405") == 0 && wire.output.find("Allow: GET, HEAD\r\n") != std::string::npos,
          "static method rejection includes Allow");
}
void deadlinesAndFailures() {
    Wire wire; HttpApiPort api(wire); WriteRequest request;
    wire.connect(""); poll(api, wire, 0xfffffff0u); poll(api, wire, 1983);
    check(wire.output.empty(), "receive deadline survives clock wrap before boundary");
    poll(api, wire, 1984); drain(api, wire, 1985);
    check(wire.output.find("HTTP/1.1 408") == 0 && !api.takeRequest(request), "timeout never delivers a command");
    wire.connect("GET / HTTP/1.1\r\n"); wire.eof = true;
    poll(api, wire, 0); drain(api, wire);
    check(wire.output.find("HTTP/1.1 400") == 0, "truncated peer EOF gets bounded error response");
    wire.connect(get("/")); poll(api, wire, 0); poll(api, wire, 1);
    wire.stall = true; poll(api, wire, 5000);
    check(wire.active, "slow response retains original total deadline");
    poll(api, wire, 5001); check(!wire.active && wire.output.empty(), "stalled send closes at deadline");
    wire.connect(get("/api/v1/state")); poll(api, wire, 0xfffffff0u); poll(api, wire, 0xfffffff1u);
    poll(api, wire, 4985); check(!api.takeRequest(request) && !wire.active, "queued API expires across clock wrap");
    wire.connect(get("/api/v1/state")); poll(api, wire, 0); poll(api, wire, 1);
    check(api.takeRequest(request), "complete request delivered");
    poll(api, wire, 5001); api.sendResponse(StateResponse{});
    check(!wire.active && wire.output.empty(), "response after expired application slot is ignored");
    wire.connect(get("/")); poll(api, wire, 0); wire.broken = true; poll(api, wire, 1);
    check(!wire.active && wire.output.empty(), "receive error closes without a response");
    wire.connect(get("/")); poll(api, wire, 0); poll(api, wire, 1); wire.broken = true; poll(api, wire, 2);
    check(!wire.active, "send failure releases the connection");
    wire.connect(get("/api/v1/state")); poll(api, wire, 0); poll(api, wire, 1); api.stop(); api.stop();
    check(!api.takeRequest(request) && !wire.active, "network shutdown discards queued request idempotently");
}
void responseOwnership() {
    Wire wire; HttpApiPort api(wire); WriteRequest request;
    wire.connect(get("/api/v1/state")); poll(api, wire, 0); poll(api, wire, 1);
    check(api.takeRequest(request) && !api.takeRequest(request), "request delivered once");
    StateResponse response; response.body = std::string("a\0b", 3);
    api.sendResponse(response); response.body = "mutated"; api.sendResponse(response);
    check(wire.output.empty(), "queueing does not write to socket");
    drain(api, wire);
    check(body(wire) == std::string("a\0b", 3), "queue owns body and ignores duplicate responses");
    for (unsigned invalid = 0; invalid < 3; ++invalid) {
        wire.connect(get("/api/v1/state")); poll(api, wire, 0); poll(api, wire, 1); api.takeRequest(request);
        StateResponse bad;
        if (invalid == 0) bad.body.assign(2049, 'x');
        if (invalid == 1) bad.allow = "GET\r\nInjected: yes";
        if (invalid == 2) bad.status = 204;
        api.sendResponse(bad); drain(api, wire);
        check(wire.output.find("HTTP/1.1 500") == 0 && wire.output.find("Injected") == std::string::npos,
              "invalid application response cannot bypass output bounds or split headers");
    }
}
struct Output : OutputPort {
    bool fan = true;
    void initializeOff() override { fan = false; }
    void setFan(bool on) override { fan = on; }
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
void appIntegration() {
    Wire wire; HttpApiPort api(wire); Output output; Storage disk; ConfigStore store(disk); Log log;
    WritePolicy policy; policy.allowedOrigins = {"http://fan.local"};
    AppLoop app("http-test", store, output, log, 20, nullptr, nullptr, &api, policy);
    app.start();
    wire.connect(put("/api/v1/power", "{\"on\":true}") + put("/api/v1/power", "{\"on\":false}"));
    poll(api, wire, 0); app.step(0, false);
    poll(api, wire, 1); check(!output.fan, "parsing does not change control output"); app.step(1, false);
    check(output.fan && wire.output.empty(), "real API applies output before any response bytes");
    wire.stall = true;
    poll(api, wire, 20); app.step(20, false); // establish debounced startup release
    poll(api, wire, 30); app.step(30, true);
    poll(api, wire, 50); app.step(50, true);
    check(!output.fan && app.snapshot().lastCommandSource == CommandSource::Button,
          "local button continues while client cannot read response");
    wire.stall = false; drain(api, wire, 51);
    check(body(wire).find("\"fanOutputOn\":true") != std::string::npos && app.snapshot().revision == 2,
          "response retains request snapshot; pipelined second command never executes");
    wire.connect(get("/api/v1/state")); poll(api, wire, 1000); app.step(1000, false);
    poll(api, wire, 1001); app.step(1001, false); drain(api, wire, 1002);
    check(body(wire).find("\"fanOutputOn\":false") != std::string::npos && app.snapshot().revision == 2,
          "new client sees current state; status read has no control side effects");
    wire.connect(get("/api/v1/state", "HEAD")); poll(api, wire, 2000); poll(api, wire, 2001);
    app.step(2001, false); drain(api, wire, 2002);
    check(wire.output.find("HTTP/1.1 405") == 0 && body(wire).empty() &&
          wire.output.find("Allow: GET\r\n") != std::string::npos, "API method policy preserved and HEAD body suppressed");
}
int main() {
    assetsAndRouting(); deadlinesAndFailures(); responseOwnership(); appIntegration();
    std::cout << "PASS: bounded HTTP connection, embedded resources, partial responses and AppLoop (synthetic transport)\n";
}
