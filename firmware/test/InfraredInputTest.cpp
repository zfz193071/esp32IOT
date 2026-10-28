#include "input/InfraredInput.h"
#include <cstdlib>
#include <deque>
#include <iostream>
#include <limits>

using namespace esp32fan;
void check(bool ok, const char* rule) {
    if (!ok) { std::cerr << "FAIL: " << rule << '\n'; std::exit(EXIT_FAILURE); }
}
struct Port : InfraredPort {
    unsigned reads = 0;
    std::deque<InfraredFrame> queue;
    bool takeFrame(InfraredFrame& frame) override {
        ++reads;
        if (queue.empty()) return false;
        frame = queue.front(); queue.pop_front(); return true;
    }
};
// Synthetic identities only, never a board/remote configuration.
InfraredBinding binding(std::uint64_t command, InfraredAction action) {
    InfraredBinding result;
    result.protocol = 7; result.address = 42; result.command = command; result.action = action;
    return result;
}
InfraredFrame frame(std::uint64_t command, std::uint32_t time, bool repeat = false) {
    InfraredFrame result;
    result.protocol = 7; result.address = 42; result.command = command;
    result.receivedAtMs = time; result.valid = true; result.repeat = repeat;
    return result;
}
int main() {
    Port port;
    const auto toggle = binding(9, InfraredAction::Toggle);
    const auto on = binding(1, InfraredAction::On);
    const auto off = binding(2, InfraredAction::Off);
    InfraredInput input(port, {on, off, toggle});
    check(input.configured(), "valid explicit mappings enable input");
    auto emit = [&](InfraredFrame value, std::uint32_t now) {
        port.queue.push_back(value); return input.update(now);
    };
    check(emit(frame(9, 0, true), 0) == InfraredAction::None, "orphan repeat cannot issue a command");
    check(emit(frame(9, 1), 1) == InfraredAction::Toggle, "initial full frame issues one action");
    for (std::uint32_t t = 101; t <= 3001; t += 100) {
        check(emit(frame(9, t, t % 200 == 101), t) == InfraredAction::None,
              "three-second hold with mixed full/repeat frames does not toggle again");
    }
    check(emit(frame(9, 3251), 3251) == InfraredAction::Toggle,
          "new full frame at release-gap boundary permits another press");
    check(emit(frame(2, 3252), 3252) == InfraredAction::Off,
          "different known key is accepted without same-key cooldown");
    auto unknown = frame(1, 3253); unknown.address = 43;
    check(emit(unknown, 3253) == InfraredAction::None, "foreign remote address ignored");
    unknown = frame(1, 3254); unknown.protocol = 8;
    check(emit(unknown, 3254) == InfraredAction::None, "foreign protocol ignored");
    check(emit(frame(999, 3255), 3255) == InfraredAction::None, "unknown command ignored");
    unknown = frame(1, 3256); unknown.valid = false;
    check(emit(unknown, 3256) == InfraredAction::None, "failed decoder validation ignored");
    check(emit(frame(2, 3257), 3257) == InfraredAction::None,
          "unknown traffic does not reset repeat filtering for the last known key");
    check(emit(frame(1, 3256), 3258) == InfraredAction::None, "out-of-order captured frame ignored");
    check(emit(frame(1, 3260), 3510) == InfraredAction::On, "maximum allowed queue age is inclusive");
    check(emit(frame(2, 3261), 3512) == InfraredAction::None, "stale queue frame is not replayed");
    check(emit(frame(2, 4000), 3999) == InfraredAction::None, "future timestamp rejected");
    check(emit(frame(1, 4001, true), 4001) == InfraredAction::None, "late repeat never becomes a new press");
    port.queue.push_back(frame(1, 4100));
    port.queue.push_back(frame(2, 4101));
    const auto reads = port.reads;
    input.update(4101);

    Port longIdlePort;
    InfraredInput longIdle(longIdlePort, {toggle});
    longIdlePort.queue.push_back(frame(9, 1));
    check(longIdle.update(1) == InfraredAction::Toggle, "first key before long idle accepted");
    longIdlePort.queue.push_back(frame(9, 200));
    check(longIdle.update(450) == InfraredAction::None,
          "queued full frame captured within the hold gap stays suppressed at maximum age");
    check(longIdle.update(700) == InfraredAction::None, "recent history is retained at age plus gap boundary");
    longIdlePort.queue.push_back(frame(9, 450));
    check(longIdle.update(700) == InfraredAction::Toggle,
          "old queued full frame at inclusive age is ordered by capture time");
    longIdle.update(951); // Release history before the half-range clock boundary.
    longIdlePort.queue.push_back(frame(9, 0x80000020u));
    check(longIdle.update(0x80000020u) == InfraredAction::Toggle,
          "first press after more than half a clock cycle idle is accepted");
    longIdlePort.queue.push_back(frame(9, 0x80000021u));
    check(longIdle.update(0x80000021u) == InfraredAction::None,
          "long-idle recovery still suppresses immediate full-frame repeats");
    Port idleWithoutUpdates;
    InfraredInput resumed(idleWithoutUpdates, {toggle});
    idleWithoutUpdates.queue.push_back(frame(9, 1));
    check(resumed.update(1) == InfraredAction::Toggle, "first key before paused loop accepted");
    idleWithoutUpdates.queue.push_back(frame(9, 0x80000020u));
    check(resumed.update(0x80000020u) == InfraredAction::Toggle,
          "first fresh frame after a long loop pause expires obsolete hold history");
    check(port.reads == reads + 1 && port.queue.size() == 1, "one loop consumes at most one IR frame");
    input.update(4101);

    Port wrapped;
    InfraredInput wrap(wrapped, {toggle});
    const auto max = std::numeric_limits<std::uint32_t>::max();
    wrapped.queue.push_back(frame(9, max - 100));
    check(wrap.update(max - 100) == InfraredAction::Toggle, "initial frame before wrap accepted");
    wrapped.queue.push_back(frame(9, 0, true));
    check(wrap.update(0) == InfraredAction::None, "repeat continuity survives uint32 wrap");
    wrapped.queue.push_back(frame(9, 249));
    check(wrap.update(249) == InfraredAction::None, "full frame within release gap is suppressed");
    wrapped.queue.push_back(frame(9, 499));
    check(wrap.update(499) == InfraredAction::Toggle, "new press after wrapped gap accepted");

    for (const auto& map : {std::vector<InfraredBinding>{}, std::vector<InfraredBinding>{on, on},
                           std::vector<InfraredBinding>{on, off, toggle, binding(3, InfraredAction::On)},
                           std::vector<InfraredBinding>{binding(4, InfraredAction::None)}}) {
        Port disabled;
        InfraredInput bad(disabled, map);
        check(!bad.configured() && bad.update(0) == InfraredAction::None && disabled.reads == 0,
              "empty/duplicate/oversized/invalid mapping disables input without consuming frames");
    }
    Port disabled;
    InfraredTiming invalid;
    invalid.releaseGapMs = 0;
    check(!InfraredInput(disabled, {on}, invalid).configured(), "zero release gap rejected");
    invalid.releaseGapMs = 250; invalid.maxFrameAgeMs = 0x80000000u;
    check(!InfraredInput(disabled, {on}, invalid).configured(), "ambiguous half-range clock limit rejected");
    invalid.releaseGapMs = 0x7fffffffu; invalid.maxFrameAgeMs = 1;
    check(!InfraredInput(disabled, {on}, invalid).configured(),
          "release and queue-age window must fit inside the half-range clock limit");
    std::cout << "PASS: infrared mapping, hold filtering, frame age and wrap (host only)\n";
}
