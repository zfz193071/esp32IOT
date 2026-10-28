#include "adapters/WriteApi.h"
#include <cstdlib>
#include <iostream>

using namespace esp32fan;
void check(bool ok, const char* rule) {
    if (!ok) { std::cerr << "FAIL: " << rule << '\n'; std::exit(EXIT_FAILURE); }
}
class MemoryPersistence : public ConfigPersistence {
public:
    bool fail = false;
    unsigned writes = 0;
    ReadResult read(ConfigRecord&) override { return ReadResult::Missing; }
    bool replaceAtomically(const ConfigRecord&) override { ++writes; return !fail; }
};
int main() {
    MemoryPersistence disk;
    ConfigStore store(disk);
    store.load();
    FanController fan("api-boot", store.current());
    WritePolicy policy;
    policy.allowedOrigins = {"http://192.168.1.20", "http://fan.local:8080"};
    WriteRequest request;
    request.method = "PUT";
    request.path = "/api/v1/power";
    request.host = "192.168.1.20";
    request.hasOrigin = true;
    request.origin = "http://192.168.1.20";
    request.contentType = "application/json";
    request.body = "{\"on\":true}";
    auto send = [&]() { return handleWriteRequest(request, policy, fan, store); };
    auto snapshot = [&]() { return handleStateRequest("GET", "/api/v1/state", fan.snapshot()).body; };
    auto reject = [&](int expected, const char* rule) {
        const auto before = snapshot();
        const auto writes = disk.writes;
        const auto response = send();
        check(response.status == expected && snapshot() == before && disk.writes == writes, rule);
        check(std::string(response.cacheControl) == "no-store", "errors disable caching");
    };
    check(send().status == 200 && fan.snapshot().fanOutputOn, "explicit on reaches controller");
    const auto revision = fan.snapshot().revision;
    check(send().status == 200 && fan.snapshot().revision == revision, "repeated on is idempotent");
    for (const auto body : {"{}", "null", "[]", "{\"on\":1}", "{\"on\":\"true\"}",
         "{\"on\":null}", "{\"on\":false,\"extra\":1}", "{\"on\":false,\"on\":true}",
         "{\"on\":false,\"\\u006fn\":true}", "{\"on\":false,}", "{\"on\":false}{}",
         "{\"on\":false /* comment */}", "{\"on\":{}}", "{\"on\":truefalse}",
         "{\"\\x6fn\":false}", "{\"on\":false"}) {
        request.body = body;
        reject(400, "malformed/duplicate/extra/wrong type command never mutates");
    }
    request.body = " \n{ \"\\u006fn\" : false }\t";
    check(send().status == 200 && !fan.snapshot().fanOutputOn, "JSON whitespace and escaped ASCII keys decode");
    request.body = "{\"on\":true}";
    request.origin = "http://evil.example"; reject(403, "foreign origin denied");
    request.origin = "null"; reject(403, "opaque origin denied");
    request.origin.clear(); reject(403, "empty present origin denied");
    request.hasOrigin = false; reject(403, "originless diagnostics disabled by default");
    policy.allowOriginlessDiagnostics = true;
    check(send().status == 200, "explicit originless diagnostics policy allows trusted host");
    request.host = "evil.example"; reject(403, "originless request still requires trusted host");
    request.hasOrigin = true; request.origin = "http://evil.example";
    reject(403, "matching attacker Host/Origin pair is not trusted");
    request.host = "fan.local:8080"; request.origin = "http://fan.local:8080";
    check(send().status == 200, "configured authority with port is permitted");
    request.origin = "http://fan.local"; reject(403, "origin port mismatch denied");
    request.origin = "http://fan.local:8080";
    request.ambiguousHeaders = true; reject(400, "ambiguous HTTP headers rejected");
    request.ambiguousHeaders = false;
    for (const auto type : {"text/plain", "application/ json", "application/json; charset=latin1",
                           "application/json; charset=utf-8; extra=x"}) {
        request.contentType = type; reject(400, "invalid media type rejected");
    }
    request.contentType = " Application/JSON ; charset = UTF-8 ";
    check(send().status == 200, "media type case and parameter whitespace supported");
    request.body.assign(1025, ' '); reject(413, "body upper bound enforced");
    request.body = "{\"on\":false}";
    request.method = "GET"; reject(405, "GET never performs writes");
    check(std::string(send().allow) == "PUT", "405 advertises PUT");
    request.method = "PUT"; request.path = "/unknown"; reject(404, "unknown route rejected");
    request.path.assign(129, 'x'); reject(414, "path upper bound enforced");
    request.path = "/api/v1/mode";
    request.body = "{\"mode\":\"manual\"}";
    check(send().status == 200 && fan.snapshot().fanOutputOn, "manual mode keeps output");
    request.body = "{\"mode\":\"auto\"}";
    reject(409, "missing temperature blocks auto without changing state");
    check(send().body.find("TEMPERATURE_UNAVAILABLE") != std::string::npos, "409 exposes stable code");
    fan.recordTemperature(29, 0);
    check(send().status == 200 && fan.snapshot().mode == Mode::Auto && !fan.snapshot().fanOutputOn,
          "automatic admission uses real controller close baseline");
    request.body = "{\"mode\":\"AUTO\"}"; reject(400, "mode enum is strict");
    request.path = "/api/v1/config";
    for (const auto body : {"{\"onThresholdC\":30}", "{\"onThresholdC\":30,\"offThresholdC\":29.5}",
         "{\"onThresholdC\":30,\"offThresholdC\":-1}", "{\"onThresholdC\":61,\"offThresholdC\":28}",
         "{\"onThresholdC\":NaN,\"offThresholdC\":28}", "{\"onThresholdC\":1e999,\"offThresholdC\":28}",
         "{\"onThresholdC\":030,\"offThresholdC\":28}", "{\"onThresholdC\":+30,\"offThresholdC\":28}",
         "{\"onThresholdC\":30.,\"offThresholdC\":28}", "{\"onThresholdC\":30e,\"offThresholdC\":28}",
         "{\"onThresholdC\":\"30\",\"offThresholdC\":28}", "{\"onThresholdC\":30,\"offThresholdC\":28,\"x\":0}"}) {
        request.body = body; reject(400, "config strict grammar, completeness, bounds and types enforced");
    }
    request.body = "{\"offThresholdC\":2.7e1,\"onThresholdC\":29}";
    check(send().status == 200 && fan.snapshot().fanOutputOn && disk.writes == 1,
          "reverse field order and exponent accepted; persisted config recalculates automatic output");
    check(send().status == 200 && disk.writes == 1, "identical config does not write again");
    disk.fail = true;
    request.body = "{\"onThresholdC\":40,\"offThresholdC\":38}";
    const auto beforeFailure = snapshot();
    const auto failure = send();
    check(failure.status == 503 && failure.body.find("STORAGE_FAILURE") != std::string::npos &&
              snapshot() == beforeFailure, "storage failure preserves full controller snapshot");
    request.path = "/api/v1/power"; request.body = "{\"on\":false}";
    check(send().status == 200 && fan.snapshot().mode == Mode::Manual && !fan.snapshot().fanOutputOn,
          "web manual off takes over automatic control");
    FanController invalidBoot("");
    check(handleWriteRequest(request, policy, invalidBoot, store).status == 503 &&
              invalidBoot.snapshot().revision == 0, "invalid response state blocks writes before mutation");
    std::cout << "PASS: C++ write API parsing, origin policy, control and config failures (host only)\n";
}
