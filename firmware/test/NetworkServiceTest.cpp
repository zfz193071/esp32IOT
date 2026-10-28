#include "services/NetworkService.h"
#include "domain/FanController.h"
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace esp32fan;
void check(bool ok, const char* rule) {
    if (!ok) { std::cerr << "FAIL: " << rule << '\n'; std::exit(EXIT_FAILURE); }
}
class FakeNetwork : public NetworkPort {
public:
    bool beginOk = true;
    NetworkLink link = NetworkLink::Disconnected;
    std::string ip = "192.168.1.20";
    unsigned begins = 0, stops = 0, reads = 0;
    bool begin() override { ++begins; if (beginOk) link = NetworkLink::Connecting; return beginOk; }
    NetworkLink status() override { ++reads; return link; }
    std::string address() override { return ip; }
    void disconnect() override { ++stops; link = NetworkLink::Disconnected; }
};
int main() {
    FakeNetwork port;
    NetworkService service(port);
    FanController fan("network-test");
    auto cycle = [&](std::uint32_t now) {
        const auto update = service.update(now);
        fan.setWifiConnected(update.connected);
        return update;
    };
    check(cycle(0).diagnostic == NetworkDiagnostic::Connecting && port.begins == 1,
          "first cycle begins one connection without waiting");
    fan.toggleFromButton();
    for (std::uint32_t t = 1; t <= 100; ++t) cycle(t);
    check(port.begins == 1 && fan.snapshot().fanOutputOn && !fan.snapshot().wifiConnected,
          "pending network does not block local input or trigger repeated begin");
    port.link = NetworkLink::Connected;
    auto update = cycle(101);
    check(update.diagnostic == NetworkDiagnostic::Connected && update.address == port.ip &&
              fan.snapshot().wifiConnected && fan.snapshot().fanOutputOn,
          "connection publishes IP while preserving manual output");
    const auto connectedRevision = fan.snapshot().revision;
    check(cycle(102).diagnostic == NetworkDiagnostic::None &&
              fan.snapshot().revision == connectedRevision, "unchanged connection has no diagnostic or revision churn");
    port.ip = "192.168.1.21";
    check(cycle(103).diagnostic == NetworkDiagnostic::AddressChanged &&
              fan.snapshot().revision == connectedRevision, "IP change is reported separately from device state");
    fan.recordTemperature(40, 104);
    fan.setMode(Mode::Auto, CommandSource::Web);
    port.link = NetworkLink::Disconnected;
    update = cycle(105);
    check(update.diagnostic == NetworkDiagnostic::Disconnected && update.address.empty() &&
              !fan.snapshot().wifiConnected && fan.snapshot().mode == Mode::Auto && fan.snapshot().fanOutputOn,
          "disconnect clears stale IP but preserves automatic output");
    const auto stopped = port.stops;
    cycle(5104);
    check(port.begins == 1 && port.stops == stopped, "retry delay does not spam begin/disconnect");
    check(cycle(5105).diagnostic == NetworkDiagnostic::Connecting && port.begins == 2,
          "retry occurs at exact failure-relative delay");
    port.link = NetworkLink::Connected;
    check(cycle(5106).connected && fan.snapshot().mode == Mode::Auto,
          "network recovery does not change controller mode");
    // Health clock remains the App's independent responsibility.
    fan.tick(5106);
    check(fan.snapshot().mode == Mode::Manual && !fan.snapshot().fanOutputOn,
          "temperature expiry still applies independently of network recovery");

    FakeNetwork failed;
    failed.beginOk = false;
    NetworkService failures(failed);
    check(failures.update(0).diagnostic == NetworkDiagnostic::StartFailed && failed.stops == 1,
          "failed begin is cancelled and reported");
    failures.update(4999);
    check(failed.begins == 1, "immediate failures are rate-limited");
    failures.update(5000);
    check(failed.begins == 2, "immediate failure retries after configured delay");

    FakeNetwork timeout;
    NetworkService deadlines(timeout);
    deadlines.update(0);
    check(deadlines.update(9999).diagnostic == NetworkDiagnostic::None, "connection remains pending before deadline");
    timeout.link = NetworkLink::Connected;
    check(deadlines.update(10000).diagnostic == NetworkDiagnostic::Timeout && timeout.stops == 1,
          "deadline cancels even a late connected result");
    deadlines.update(14999);
    check(timeout.begins == 1, "timeout retry is based on failure time, not previous start");
    deadlines.update(15000);
    check(timeout.begins == 2, "timeout retries once after cooldown");
    timeout.link = NetworkLink::Disconnected;
    check(deadlines.update(15001).diagnostic == NetworkDiagnostic::ConnectFailed,
          "asynchronous authentication/link failure does not wait for whole timeout");
    deadlines.update(1000000);
    check(timeout.begins == 3, "long scheduling gap does not create reconnect burst");

    FakeNetwork noAddress;
    noAddress.link = NetworkLink::Connected;
    noAddress.ip.clear();
    NetworkService noIp(noAddress);
    check(noIp.update(0).diagnostic == NetworkDiagnostic::MissingAddress && !noIp.update(1).connected,
          "link without usable address cannot claim online");

    FakeNetwork wrapped;
    NetworkService wraps(wrapped);
    const auto max = std::numeric_limits<std::uint32_t>::max();
    wraps.update(max - 100);
    check(wraps.update(9898).diagnostic == NetworkDiagnostic::None, "timeout handles wrap just below boundary");
    check(wraps.update(9899).diagnostic == NetworkDiagnostic::Timeout, "timeout handles exact boundary after wrap");
    wraps.update(14898);
    check(wrapped.begins == 1, "retry handles wrap before boundary");
    wraps.update(14899);
    check(wrapped.begins == 2, "retry handles wrap at boundary");

    for (const auto invalid : {0u, 0x80000000u}) {
        FakeNetwork disabled;
        NetworkTiming timing;
        timing.connectTimeoutMs = invalid;
        NetworkService bad(disabled, timing);
        check(bad.update(0).diagnostic == NetworkDiagnostic::InvalidTiming &&
                  bad.update(1).diagnostic == NetworkDiagnostic::None && disabled.reads == 0 && disabled.begins == 0,
              "invalid deadline emits one diagnostic without driver access");
        timing.connectTimeoutMs = 10000;
        timing.retryDelayMs = invalid;
        NetworkService badRetry(disabled, timing);
        check(badRetry.update(0).diagnostic == NetworkDiagnostic::InvalidTiming, "invalid retry delay is rejected");
    }
    std::cout << "PASS: nonblocking network connection, retry pacing and local control (host only)\n";
}
