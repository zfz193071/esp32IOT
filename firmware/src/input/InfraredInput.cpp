#include "InfraredInput.h"
#include <utility>

namespace esp32fan {
InfraredInput::InfraredInput(InfraredPort& port, std::vector<InfraredBinding> bindings, InfraredTiming timing)
    : port_(port), bindings_(std::move(bindings)), timing_(timing) {
    if (bindings_.empty() || bindings_.size() > 3 || timing_.releaseGapMs == 0 ||
        timing_.maxFrameAgeMs == 0 || timing_.releaseGapMs >= 0x80000000u ||
        timing_.maxFrameAgeMs >= 0x80000000u) return;
    for (std::size_t i = 0; i < bindings_.size(); ++i) {
        const auto& binding = bindings_[i];
        if (binding.protocol == 0 || (binding.action != InfraredAction::On &&
            binding.action != InfraredAction::Off && binding.action != InfraredAction::Toggle)) return;
        for (std::size_t j = 0; j < i; ++j) {
            if (binding.protocol == bindings_[j].protocol && binding.address == bindings_[j].address &&
                binding.command == bindings_[j].command) return;
        }
    }
    configured_ = true;
}
bool InfraredInput::configured() const { return configured_; }
InfraredAction InfraredInput::update(std::uint32_t nowMs) {
    if (!configured_) return InfraredAction::None;
    InfraredFrame frame;
    if (!port_.takeFrame(frame) || !frame.valid ||
        static_cast<std::uint32_t>(nowMs - frame.receivedAtMs) > timing_.maxFrameAgeMs) return InfraredAction::None;
    for (std::size_t i = 0; i < bindings_.size(); ++i) {
        const auto& binding = bindings_[i];
        if (frame.protocol != binding.protocol || frame.address != binding.address ||
            frame.command != binding.command) continue;
        const auto elapsed = static_cast<std::uint32_t>(frame.receivedAtMs - lastSeenMs_);
        // Discard out-of-order timestamps instead of mistaking them for a release.
        if (haveLast_ && elapsed >= 0x80000000u) return InfraredAction::None;
        const bool same = haveLast_ && lastBinding_ == i;
        if (frame.repeat) {
            if (same && elapsed < timing_.releaseGapMs) lastSeenMs_ = frame.receivedAtMs;
            return InfraredAction::None;
        }
        const bool held = same && elapsed < timing_.releaseGapMs;
        haveLast_ = true;
        lastBinding_ = i;
        lastSeenMs_ = frame.receivedAtMs;
        return held ? InfraredAction::None : binding.action;
    }
    return InfraredAction::None;
}
} // namespace esp32fan
