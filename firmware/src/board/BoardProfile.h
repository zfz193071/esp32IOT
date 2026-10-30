#pragma once

namespace esp32fan {

// -1 means that a signal has not been assigned. No motor pin is guessed.
struct BoardProfile {
    int fanEnableGpio = -1;
    int buttonGpio = -1;
    int statusLedGpio = -1;
    int infraredGpio = -1;
    int temperatureGpio = -1;
    bool fanActiveHigh = true;
    bool buttonActiveLow = true;
    bool ledActiveHigh = true;
    bool hasInfrared = true;
    bool hasTemperature = false;
};

enum class ProfileError {
    None,
    MissingSignal,
    InvalidGpio,
    OutputOnInputOnlyGpio,
    FanOnBootStrapGpio,
    GpioConflict
};

// Checks a classic ESP32-WROOM-32 candidate before any GPIO is configured.
// Electrical load, supply, wiring and reset behavior require board validation.
ProfileError validateBoardProfile(const BoardProfile& profile);

} // namespace esp32fan
