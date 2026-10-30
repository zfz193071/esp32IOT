#include "BoardProfile.h"

namespace esp32fan {
namespace {
bool usableGpio(int gpio) {
    return gpio >= 0 && gpio <= 39 && !(gpio >= 6 && gpio <= 11) &&
           gpio != 20 && gpio != 24 && gpio != 28 && gpio != 29 && gpio != 30 && gpio != 31;
}
bool inputOnlyGpio(int gpio) { return gpio >= 34 && gpio <= 39; }
bool bootStrapGpio(int gpio) {
    return gpio == 0 || gpio == 2 || gpio == 4 || gpio == 5 || gpio == 12 || gpio == 15;
}
} // namespace

ProfileError validateBoardProfile(const BoardProfile& profile) {
    const int pins[] = {profile.fanEnableGpio, profile.buttonGpio, profile.statusLedGpio,
                        profile.infraredGpio, profile.temperatureGpio};
    const bool enabled[] = {true, true, true, profile.hasInfrared, profile.hasTemperature};
    for (int i = 0; i < 5; ++i) {
        if (!enabled[i]) continue;
        if (pins[i] == -1) return ProfileError::MissingSignal;
        if (!usableGpio(pins[i])) return ProfileError::InvalidGpio;
        if ((i == 0 || i == 2) && inputOnlyGpio(pins[i]))
            return ProfileError::OutputOnInputOnlyGpio;
        for (int j = 0; j < i; ++j)
            if (enabled[j] && pins[i] == pins[j]) return ProfileError::GpioConflict;
    }
    if (bootStrapGpio(profile.fanEnableGpio)) return ProfileError::FanOnBootStrapGpio;
    return ProfileError::None;
}
} // namespace esp32fan
