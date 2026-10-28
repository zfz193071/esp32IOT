#include "ButtonDebouncer.h"

namespace esp32fan {

ButtonDebouncer::ButtonDebouncer(std::uint32_t debounceMs)
    : debounceMs_(debounceMs) {}

bool ButtonDebouncer::update(bool pressed, std::uint32_t nowMs) {
    if (!initialized_) {
        initialized_ = true;
        candidatePressed_ = pressed;
        stablePressed_ = pressed;
        candidateSinceMs_ = nowMs;
    }
    if (pressed != candidatePressed_) {
        candidatePressed_ = pressed;
        candidateSinceMs_ = nowMs;
    }
    if (static_cast<std::uint32_t>(nowMs - candidateSinceMs_) < debounceMs_) {
        return false;
    }
    if (!candidatePressed_) {
        stablePressed_ = false;
        armed_ = true;
        return false;
    }
    if (!stablePressed_) {
        stablePressed_ = true;
        if (armed_) {
            armed_ = false;
            return true;
        }
    }
    return false;
}

} // namespace esp32fan
