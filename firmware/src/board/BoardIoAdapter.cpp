#include "BoardIoAdapter.h"

namespace esp32fan {
BoardIoAdapter::BoardIoAdapter(GpioPort& gpio, const BoardProfile& profile)
    : gpio_(gpio), profile_(profile), error_(validateBoardProfile(profile)) {}

void BoardIoAdapter::initializeOff() {
    if (error_ != ProfileError::None || initialized_) return;
    gpio_.configureOutput(profile_.fanEnableGpio, !profile_.fanActiveHigh);
    gpio_.configureOutput(profile_.statusLedGpio, !profile_.ledActiveHigh);
    gpio_.configureInput(profile_.buttonGpio, profile_.buttonActiveLow);
    initialized_ = true;
}

bool BoardIoAdapter::buttonPressed() {
    if (!initialized_) return false;
    const bool high = gpio_.read(profile_.buttonGpio);
    return profile_.buttonActiveLow ? !high : high;
}

void BoardIoAdapter::setFan(bool on) {
    if (initialized_) gpio_.write(profile_.fanEnableGpio, on == profile_.fanActiveHigh);
}

void BoardIoAdapter::setIndicator(bool on) {
    if (initialized_) gpio_.write(profile_.statusLedGpio, on == profile_.ledActiveHigh);
}
} // namespace esp32fan
