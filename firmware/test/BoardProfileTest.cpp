#include "board/BoardProfile.h"
#include <cstdlib>
#include <iostream>

using namespace esp32fan;
static void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    BoardProfile profile;
    check(validateBoardProfile(profile) == ProfileError::MissingSignal,
          "no motor pin or other default wiring can pass validation");
    profile.fanEnableGpio = 23;
    profile.buttonGpio = 27;
    profile.statusLedGpio = 15;
    profile.infraredGpio = 14;
    check(validateBoardProfile(profile) == ProfileError::None, "complete M4 candidate");
    profile.hasTemperature = true;
    check(validateBoardProfile(profile) == ProfileError::MissingSignal, "M5 requires sensor assignment");
    profile.temperatureGpio = 13;
    check(validateBoardProfile(profile) == ProfileError::None, "complete M5 candidate");
    profile.fanEnableGpio = 15;
    check(validateBoardProfile(profile) == ProfileError::GpioConflict,
          "example motor and LED both on GPIO15 must be rejected");
    profile.fanEnableGpio = 0;
    check(validateBoardProfile(profile) == ProfileError::FanOnBootStrapGpio,
          "motor must not use boot strap pins");
    profile.fanEnableGpio = 34;
    check(validateBoardProfile(profile) == ProfileError::OutputOnInputOnlyGpio,
          "motor cannot use input-only pin");
    profile.fanEnableGpio = 6;
    check(validateBoardProfile(profile) == ProfileError::InvalidGpio, "flash pin is reserved");
    profile.fanEnableGpio = 23;
    profile.infraredGpio = 27;
    check(validateBoardProfile(profile) == ProfileError::GpioConflict,
          "example infrared and button on one GPIO must be rejected");
    profile.hasInfrared = false;
    check(validateBoardProfile(profile) == ProfileError::None,
          "disabled optional input does not reserve its candidate pin");
    std::cout << "PASS: candidate board pin validation (no physical wiring verified)\n";
}
