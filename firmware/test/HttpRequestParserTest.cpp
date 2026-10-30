#include "adapters/HttpRequestParser.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace esp32fan;
void check(bool condition, const char* rule) {
    if (!condition) { std::cerr << "FAIL: " << rule << '\n'; std::exit(1); }
}

const std::string host = "Host: 192.168.1.20\r\n";
const std::string origin = "Origin: http://192.168.1.20\r\n";
const std::string type = "Content-Type: application/json\r\n";
std::string put(const std::string& body, const std::string& path = "/api/v1/power") {
    return "PUT " + path + " HTTP/1.1\r\n" + host + origin + type +
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}
std::size_t feedAll(HttpRequestParser& parser, const std::string& bytes, std::uint32_t now = 0) {
    std::size_t offset = 0;
    while (offset < bytes.size() && parser.status() == HttpParseStatus::Receiving) {
        const auto consumed = parser.feed(bytes.data() + offset, bytes.size() - offset, now);
        check(consumed > 0 && consumed <= HttpRequestParser::maxBytesPerFeed,
              "every receiving call makes bounded progress");
        offset += consumed;
    }
    return offset;
}
void rejected(const std::string& bytes, int code) {
    HttpRequestParser parser(0);
    feedAll(parser, bytes);
    check(parser.status() == HttpParseStatus::Rejected && parser.errorStatus() == code,
          "invalid input has expected rejection status");
    WriteRequest destination;
    destination.path = "unchanged";
    check(!parser.takeRequest(destination) && destination.path == "unchanged",
          "invalid request never exposes partial data");
    check(parser.feed("GET / HTTP/1.1\r\n", 16, 0) == 0,
          "rejection is terminal and cannot be revived by more bytes");
}
struct Storage : ConfigPersistence {
    unsigned writes = 0;
    ReadResult read(ConfigRecord&) override { return ReadResult::Missing; }
    PersistenceResult replace(const ConfigRecord&) override { ++writes; return PersistenceResult::Saved; }
};

int main() {
    const auto bytes = put("{\"on\":true}");
    for (std::size_t split = 0; split <= bytes.size(); ++split) {
        HttpRequestParser parser(0);
        WriteRequest request;
        request.path = "unchanged";
        check(parser.feed(bytes.data(), split, 0) == split, "arbitrary first fragment accepted");
        if (split < bytes.size()) {
            check(!parser.takeRequest(request) && request.path == "unchanged",
                  "incomplete fragment cannot be delivered");
            check(parser.feed(bytes.data() + split, bytes.size() - split, 1) == bytes.size() - split,
                  "second fragment completes across any boundary, including CRLF");
        }
        check(parser.status() == HttpParseStatus::Complete && parser.errorStatus() == 0 &&
                  parser.takeRequest(request) && request.method == "PUT" &&
                  request.path == "/api/v1/power" && request.host == "192.168.1.20" &&
                  request.hasOrigin && request.origin == "http://192.168.1.20" &&
                  request.contentType == "application/json" && request.body == "{\"on\":true}" &&
                  !request.ambiguousHeaders, "all request fields are preserved");
        check(!parser.takeRequest(request) && parser.feed(bytes.data(), bytes.size(), 2) == 0,
              "complete request can only be delivered once");
    }
    HttpRequestParser singleByte(0);
    for (const char byte : bytes) check(singleByte.feed(&byte, 1, 1) == 1, "one-byte fragmented input");
    check(singleByte.status() == HttpParseStatus::Complete, "single bytes form a complete request");

    HttpRequestParser get(0);
    feedAll(get, "GET /api/v1/state?unused=1 HTTP/1.1\r\nhOsT: \t fan.local:8080 \t\r\nX-Ignored: value\r\n\r\n");
    WriteRequest request;
    check(get.takeRequest(request) && request.path == "/api/v1/state" &&
              request.host == "fan.local:8080" && request.body.empty() && !request.hasOrigin,
          "query separates from exact path; header names and surrounding OWS normalize");
    HttpRequestParser emptyOrigin(0);
    feedAll(emptyOrigin, "GET / HTTP/1.1\r\n" + host + "Origin:\t\r\n\r\n");
    check(emptyOrigin.takeRequest(request) && request.hasOrigin && request.origin.empty(),
          "empty Origin remains present for downstream same-origin rejection");
    HttpRequestParser encodedPath(0);
    feedAll(encodedPath, "GET /api%2fv1/state HTTP/1.1\r\n" + host + "\r\n");
    check(encodedPath.takeRequest(request) && request.path == "/api%2fv1/state",
          "encoded path is not decoded into a privileged route");

    for (const auto& header : {"Host: x\r\nhost: x\r\n", "Origin: x\r\nORIGIN: x\r\n",
                               "Content-Type: application/json\r\ncontent-type: application/json\r\n",
                               "Content-Length: 0\r\ncontent-length: 0\r\n",
                               "Content-Length: 0, 0\r\n", "Content-Length: +1\r\n",
                               "Content-Length: -1\r\n", "Content-Length: 1x\r\n",
                               "Content-Length: 1 0\r\n", "Content-Length:\r\n",
                               "Transfer-Encoding: chunked\r\n",
                               "Content-Length: 0\r\nTransfer-Encoding: chunked\r\n",
                               "Transfer-Encoding: chunked\r\nContent-Length: 0\r\n",
                               "Host : fan.local\r\n", " Host: fan.local\r\n",
                               "X-Test: one\r\n\ttwo\r\n", "No-Colon\r\n", ": empty\r\n"}) {
        rejected("PUT /api/v1/power HTTP/1.1\r\n" + host + header + "\r\n", 400);
    }
    for (const auto& authority : {"", "user@fan.local", "fan.local/path", "fan.local?x",
                                 "fan local", "fan.local:", "fan.local:0", "fan.local:65536",
                                 "fan.local:80:90", "fan.local:abc"}) {
        rejected(std::string("GET / HTTP/1.1\r\nHost: ") + authority + "\r\n\r\n", 400);
    }
    rejected("GET / HTTP/1.1\r\n\r\n", 400);
    rejected("GET / HTTP/1.1\n" + host + "\r\n", 400);
    rejected("GET / HTTP/1.1\rX", 400);
    rejected("GET  / HTTP/1.1\r\n", 400);
    rejected("GET / HTTP/1.1 extra\r\n", 400);
    rejected("G\tET / HTTP/1.1\r\n", 400);
    rejected("GET / HTTP/1.0\r\n", 505);
    rejected("GET http://fan.local/ HTTP/1.1\r\n", 400);
    rejected("GET /#fragment HTTP/1.1\r\n", 400);
    rejected("GET /bad\\path HTTP/1.1\r\n", 400);
    rejected("GET /bad\tpath HTTP/1.1\r\n", 400);
    rejected(std::string("GET / HTTP/1.1\r\nX: ") + char(0) + "\r\n", 400);
    rejected(std::string("GET / HTTP/1.1\r\nX: ") + char(0x80) + "\r\n", 400);
    rejected("PUT /api/v1/power HTTP/1.1\r\n" + host + "\r\n", 411);
    rejected("PUT /api/v1/power HTTP/1.1\r\n" + host + "Expect: 100-continue\r\n\r\n", 417);
    rejected("PUT / HTTP/1.1\r\n" + host + "Content-Length: 1025\r\n\r\n", 413);
    rejected("PUT / HTTP/1.1\r\n" + host + "Content-Length: 18446744073709551616\r\n\r\n", 413);
    rejected("GET /" + std::string(128, 'x') + " HTTP/1.1\r\n", 414);
    rejected("GET /" + std::string(512, 'x'), 414);
    rejected("GET / HTTP/1.1\r\n" + host + "X: " + std::string(510, 'x'), 431);
    rejected("GET / HTTP/1.1\r\nHost: " + std::string(129, 'a') + "\r\n", 431);
    rejected("GET / HTTP/1.1\r\n" + host + "Origin: " + std::string(257, 'a') + "\r\n", 431);
    rejected("GET / HTTP/1.1\r\n" + host + "Content-Type: " + std::string(129, 'a') + "\r\n", 431);

    std::string padded = "GET / HTTP/1.1\r\n" + host;
    while (padded.size() + 106 < HttpRequestParser::maxHeaderBytes) padded += "X: " + std::string(100, 'x') + "\r\n";
    const auto padding = HttpRequestParser::maxHeaderBytes - padded.size() - 7;
    padded += "X: " + std::string(padding, 'x') + "\r\n\r\n";
    check(padded.size() == HttpRequestParser::maxHeaderBytes, "exact header limit fixture");
    HttpRequestParser exactHeader(0);
    feedAll(exactHeader, padded);
    check(exactHeader.status() == HttpParseStatus::Complete, "exact 2048 header bytes accepted");
    padded.insert(padded.size() - 4, "x");
    rejected(padded, 431);
    HttpRequestParser maxBody(0);
    const auto large = put(std::string(1024, 'x'));
    check(maxBody.feed(large.data(), large.size(), 0) == HttpRequestParser::maxBytesPerFeed &&
              maxBody.status() == HttpParseStatus::Receiving,
          "a ready large input is limited to 256 bytes per feed");
    feedAll(maxBody, large.substr(HttpRequestParser::maxBytesPerFeed));
    check(maxBody.takeRequest(request) && request.body.size() == 1024, "exact body limit accepted");
    HttpRequestParser binary(0);
    const std::string binaryBody{'\0', '\r', '\n', static_cast<char>(0xff)};
    feedAll(binary, put(binaryBody));
    check(binary.takeRequest(request) && request.body == binaryBody,
          "body bytes are preserved for downstream JSON validation");

    HttpRequestParser pipeline(0);
    check(feedAll(pipeline, bytes + bytes) == bytes.size() && pipeline.takeRequest(request),
          "stop at first framed request, leave pipelined bytes for transport discard");
    HttpRequestParser timeout(100);
    timeout.tick(2099);
    check(timeout.status() == HttpParseStatus::Receiving, "deadline is not early");
    check(timeout.feed(bytes.data(), bytes.size(), 2100) == 0 && timeout.errorStatus() == 408,
          "deadline wins over arriving final bytes");
    HttpRequestParser slow(0);
    slow.feed("G", 1, 0); slow.feed("E", 1, 1999); slow.tick(2000);
    check(slow.errorStatus() == 408, "dripping input does not renew total receive deadline");
    const auto start = std::numeric_limits<std::uint32_t>::max() - 1000;
    HttpRequestParser wrap(start);
    wrap.tick(start + 1999u);
    check(wrap.status() == HttpParseStatus::Receiving, "clock wrap remains before deadline");
    wrap.tick(start + 2000u);
    check(wrap.errorStatus() == 408, "clock wrap expires at exact boundary");
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        HttpRequestParser truncated(0);
        feedAll(truncated, bytes.substr(0, length));
        truncated.endOfInput();
        check(truncated.errorStatus() == 400 && !truncated.takeRequest(request),
              "EOF at any incomplete position never yields a request");
    }
    HttpRequestParser nullInput(0);
    check(nullInput.feed(nullptr, 0, 0) == 0 && nullInput.status() == HttpParseStatus::Receiving,
          "empty feed can tick without data");
    check(nullInput.feed(nullptr, 1, 0) == 0 && nullInput.errorStatus() == 400,
          "invalid nonempty buffer is rejected");

    Storage disk;
    ConfigStore store(disk);
    store.load();
    FanController fan("http-parser", store.current());
    WritePolicy policy;
    policy.allowedOrigins = {"http://192.168.1.20"};
    HttpRequestParser integration(0);
    integration.feed(bytes.data(), bytes.size() - 1, 0);
    check(!integration.takeRequest(request) && !fan.snapshot().fanOutputOn,
          "partial command has no control effect");
    integration.feed(bytes.data() + bytes.size() - 1, 1, 1);
    check(integration.takeRequest(request) &&
              handleWriteRequest(request, policy, fan, store).status == 200 && fan.snapshot().fanOutputOn,
          "complete raw request reaches existing API and controller");
    const auto revision = fan.snapshot().revision;
    check(!integration.takeRequest(request) && fan.snapshot().revision == revision,
          "delivery cannot replay the command");
    HttpRequestParser foreign(0);
    auto foreignBytes = put("{\"on\":false}");
    foreignBytes.replace(foreignBytes.find(origin), origin.size(), "Origin: http://evil.example\r\n");
    feedAll(foreign, foreignBytes);
    check(foreign.takeRequest(request) && handleWriteRequest(request, policy, fan, store).status == 403 &&
              fan.snapshot().fanOutputOn, "transport parsing does not grant cross-origin authority");
    HttpRequestParser config(0);
    feedAll(config, put("{\"onThresholdC\":35,\"offThresholdC\":32}", "/api/v1/config"));
    check(config.takeRequest(request) && handleWriteRequest(request, policy, fan, store).status == 200 &&
              disk.writes == 1 && store.current().onThresholdC == 35,
          "framed config request preserves full-body validation and persistence path");
    std::cout << "PASS: bounded HTTP parsing, fragmentation, deadlines and API integration (host only)\n";
}
