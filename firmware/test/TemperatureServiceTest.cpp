#include "services/TemperatureService.h"
#include "domain/FanController.h"
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace esp32fan;

void check(bool ok, const char* rule) {
    if (!ok) { std::cerr << "FAIL: " << rule << '\n'; std::exit(EXIT_FAILURE); }
}

class FakePort : public TemperaturePort {
public:
    unsigned starts = 0, polls = 0, cancels = 0;
    bool startOk = true;
    TemperaturePoll result = TemperaturePoll::Pending;
    double value = 29;
    bool start() override { ++starts; result = TemperaturePoll::Pending; return startOk; }
    TemperaturePoll poll(double& out) override { ++polls; out = value; return result; }
    void cancel() override { ++cancels; result = TemperaturePoll::Pending; }
};

TemperatureEvent cycle(TemperatureService& service, FanController& fan, std::uint32_t now) {
    const auto event = service.update(now);
    if (event.kind == TemperatureEventKind::Sample) fan.recordTemperature(event.temperatureC, now);
    else if (event.kind == TemperatureEventKind::Invalid) fan.invalidateTemperature(now);
    fan.tick(now);
    return event;
}

int main() {
    FakePort port;
    TemperatureService service(port);
    FanController fan("temperature-test");
    check(cycle(service, fan, 0).kind == TemperatureEventKind::None && port.starts == 1 &&
              port.polls == 0 && !fan.snapshot().hasSample &&
              fan.snapshot().sensorStatus == SensorStatus::Initializing,
          "first cycle only starts conversion; no fabricated sample");
    for (std::uint32_t t = 1; t < 100; ++t) {
        check(cycle(service, fan, t).kind == TemperatureEventKind::None,
              "pending conversion returns promptly without emitting temperature");
        if (t == 10) fan.toggleFromButton();
        if (t == 20) fan.setWifiConnected(true);
    }
    check(fan.snapshot().fanOutputOn && fan.snapshot().wifiConnected && port.starts == 1 && port.polls == 99,
          "main loop can process button/network events while waiting");
    port.result = TemperaturePoll::Ready;
    port.value = 29;
    check(cycle(service, fan, 100).kind == TemperatureEventKind::Sample &&
              fan.snapshot().temperatureC == 29 && fan.snapshot().sampleAgeMs == 0,
          "completed conversion publishes a single fresh sample");
    const auto polls = port.polls;
    cycle(service, fan, 999);
    check(port.polls == polls && port.starts == 1 && fan.snapshot().sampleAgeMs == 899,
          "cached result is not republished and does not reset sample age");
    cycle(service, fan, 1000);
    check(port.starts == 2, "next conversion starts at one-second interval boundary");
    port.result = TemperaturePoll::Ready;
    port.value = 40;
    cycle(service, fan, 1100);
    fan.setMode(Mode::Auto, CommandSource::Web);
    check(fan.snapshot().fanOutputOn, "real automatic controller consumes scheduled sample");
    cycle(service, fan, 2000);
    port.result = TemperaturePoll::Invalid;
    check(cycle(service, fan, 2100).diagnostic == TemperatureDiagnostic::ReadFailed &&
              fan.snapshot().sensorStatus == SensorStatus::Invalid && !fan.snapshot().hasTemperature &&
              fan.snapshot().sampleAgeMs == 1000 && !fan.snapshot().fanOutputOn &&
              fan.snapshot().mode == Mode::Manual && fan.snapshot().fault == Fault::TemperatureUnavailable,
          "read error invalidates temperature, retains age and invokes controller fault rules");
    cycle(service, fan, 3000);
    port.result = TemperaturePoll::Ready;
    cycle(service, fan, 3100);
    check(fan.snapshot().sensorStatus == SensorStatus::Ok && fan.snapshot().fault == Fault::None &&
              fan.snapshot().mode == Mode::Manual && !fan.snapshot().fanOutputOn,
          "new valid sample recovers health without restarting automatic output");

    cycle(service, fan, 4000);
    port.result = TemperaturePoll::Ready; // late buffered result must lose to deadline
    const auto beforeTimeoutPolls = port.polls;
    check(cycle(service, fan, 5000).diagnostic == TemperatureDiagnostic::Timeout && port.cancels == 1 &&
              port.polls == beforeTimeoutPolls, "timeout at exact deadline cancels instead of accepting late sample");
    cycle(service, fan, 5001);
    check(port.starts == 6 && port.result == TemperaturePoll::Pending, "retry cannot consume cancelled result");
    port.result = TemperaturePoll::Ready;
    port.value = std::numeric_limits<double>::quiet_NaN();
    check(cycle(service, fan, 5002).diagnostic == TemperatureDiagnostic::NonFinite,
          "nonfinite ready sample is rejected defensively");
    port.startOk = false;
    check(cycle(service, fan, 6001).diagnostic == TemperatureDiagnostic::StartFailed,
          "start failure returns explicit invalid event");
    const auto failedStarts = port.starts;
    cycle(service, fan, 6002);
    cycle(service, fan, 7000);
    check(port.starts == failedStarts, "start failures are rate-limited");
    cycle(service, fan, 7001);
    check(port.starts == failedStarts + 1, "failed conversion retries after interval");

    FakePort stopped;
    TemperatureService paused(stopped);
    FanController ageing("ageing");
    cycle(paused, ageing, 0);
    stopped.result = TemperaturePoll::Ready;
    stopped.value = 40;
    cycle(paused, ageing, 10);
    ageing.setMode(Mode::Auto, CommandSource::Web);
    // Simulate acquisition scheduling stopped while the App health clock continues.
    ageing.tick(5010);
    check(ageing.snapshot().sensorStatus == SensorStatus::Ok, "freshness exact boundary is retained");
    ageing.tick(5011);
    check(ageing.snapshot().sensorStatus == SensorStatus::Stale && !ageing.snapshot().fanOutputOn,
          "missing acquisition updates become stale independently of driver error events");

    FakePort wrapped;
    TemperatureService wrapService(wrapped);
    const auto max = std::numeric_limits<std::uint32_t>::max();
    wrapService.update(max - 100);
    wrapService.update(898);
    check(wrapped.cancels == 0, "elapsed 999ms across wrap remains pending");
    check(wrapService.update(899).diagnostic == TemperatureDiagnostic::Timeout && wrapped.cancels == 1,
          "elapsed 1000ms across wrap expires conversion");
    wrapService.update(100000);
    check(wrapped.starts == 2, "long delay starts once without catch-up burst");

    for (const auto invalidMs : {0u, 0x80000000u}) {
        FakePort invalidPort;
        TemperatureTiming timing;
        timing.intervalMs = invalidMs;
        TemperatureService invalidService(invalidPort, timing);
        check(invalidService.update(0).diagnostic == TemperatureDiagnostic::InvalidTiming &&
                  invalidService.update(1).kind == TemperatureEventKind::None && invalidPort.starts == 0,
              "invalid timing fails closed with one diagnostic and no driver calls");
        timing.intervalMs = 1000;
        timing.timeoutMs = invalidMs;
        TemperatureService invalidTimeout(invalidPort, timing);
        check(invalidTimeout.update(0).diagnostic == TemperatureDiagnostic::InvalidTiming,
              "invalid conversion deadline is rejected");
    }
    std::cout << "PASS: nonblocking temperature scheduling, failures, recovery and clock wrap (host only)\n";
}
