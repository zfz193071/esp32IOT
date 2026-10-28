#pragma once

#include <cstdint>
#include <vector>

namespace esp32fan {
enum class InfraredAction { None, On, Off, Toggle };
struct InfraredFrame {
    std::uint16_t protocol = 0; // Adapter-normalized, nonzero protocol identity.
    std::uint64_t address = 0;
    std::uint64_t command = 0;
    std::uint32_t receivedAtMs = 0;
    bool valid = false;
    bool repeat = false;
};
struct InfraredBinding {
    std::uint16_t protocol = 0;
    std::uint64_t address = 0;
    std::uint64_t command = 0;
    InfraredAction action = InfraredAction::None;
};
class InfraredPort {
public:
    virtual ~InfraredPort() = default;
    // Consume at most one decoded frame, never wait. Queue must be bounded.
    // Timestamp uses the same monotonic clock as AppLoop. Repeat-only frames
    // must be normalized to their original identity, not an invented command.
    virtual bool takeFrame(InfraredFrame& frame) = 0;
};
struct InfraredTiming {
    std::uint32_t releaseGapMs = 250;
    std::uint32_t maxFrameAgeMs = 250;
};
class InfraredInput {
public:
    // Empty, ambiguous or invalid mappings disable input; no default key codes.
    InfraredInput(InfraredPort& port, std::vector<InfraredBinding> bindings,
                  InfraredTiming timing = InfraredTiming{});
    bool configured() const;
    InfraredAction update(std::uint32_t nowMs);
private:
    InfraredPort& port_;
    std::vector<InfraredBinding> bindings_;
    InfraredTiming timing_;
    bool configured_ = false;
    bool haveLast_ = false;
    std::size_t lastBinding_ = 0;
    std::uint32_t lastSeenMs_ = 0;
};
} // namespace esp32fan
