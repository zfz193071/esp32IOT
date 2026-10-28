#pragma once

#include <cstdint>

namespace esp32fan {

// Hardware adapter normalizes GPIO polarity before calling update().
// Call from one loop with a monotonic uint32 millisecond clock (wrap allowed).
class ButtonDebouncer {
public:
    explicit ButtonDebouncer(std::uint32_t debounceMs);
    // Returns true only once for a debounced press. Release must also debounce.
    // Startup-held buttons are ignored until a stable release is observed.
    bool update(bool pressed, std::uint32_t nowMs);

private:
    const std::uint32_t debounceMs_;
    std::uint32_t candidateSinceMs_ = 0;
    bool initialized_ = false;
    bool candidatePressed_ = false;
    bool stablePressed_ = false;
    bool armed_ = false;
};

} // namespace esp32fan
