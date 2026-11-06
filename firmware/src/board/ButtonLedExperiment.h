#pragma once

#include "BoardProfile.h"
#include "GpioPort.h"
#include "../input/ButtonDebouncer.h"

namespace esp32fan {
// Standalone experiment; never configures or writes the fan output.
class ButtonLedExperiment {
public:
    ButtonLedExperiment(GpioPort& gpio, const BoardProfile& profile,
                        std::uint32_t debounceMs);
    ProfileError error() const { return error_; }
    bool initialize();
    // Returns true only when a debounced press toggles the LED.
    bool update(std::uint32_t nowMs);
    bool ledOn() const { return ledOn_; }
private:
    GpioPort& gpio_;
    const BoardProfile profile_;
    const ProfileError error_;
    ButtonDebouncer button_;
    bool initialized_ = false;
    bool ledOn_ = false;
};
} // namespace esp32fan
