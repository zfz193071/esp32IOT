#include "ButtonLedExperiment.h"

namespace esp32fan {
ButtonLedExperiment::ButtonLedExperiment(GpioPort& gpio, const BoardProfile& profile,
                                         std::uint32_t debounceMs)
    : gpio_(gpio), profile_(profile), error_(validateButtonLedProfile(profile)),
      button_(debounceMs) {}

bool ButtonLedExperiment::initialize() {
    if (error_ != ProfileError::None) return false;
    if (initialized_) return true;
    gpio_.configureOutput(profile_.statusLedGpio, !profile_.ledActiveHigh);
    gpio_.configureInput(profile_.buttonGpio, profile_.buttonActiveLow);
    initialized_ = true;
    return true;
}

bool ButtonLedExperiment::update(std::uint32_t nowMs) {
    if (!initialized_) return false;
    const bool high = gpio_.read(profile_.buttonGpio);
    if (!button_.update(profile_.buttonActiveLow ? !high : high, nowMs)) return false;
    ledOn_ = !ledOn_;
    gpio_.write(profile_.statusLedGpio, ledOn_ == profile_.ledActiveHigh);
    return true;
}
} // namespace esp32fan
