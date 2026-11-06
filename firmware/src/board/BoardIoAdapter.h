#pragma once

#include "app/AppLoop.h"
#include "board/BoardProfile.h"
#include "board/GpioPort.h"

namespace esp32fan {

// A validated profile is required. Invalid profiles never touch GPIO.
class BoardIoAdapter : public OutputPort {
public:
    BoardIoAdapter(GpioPort& gpio, const BoardProfile& profile);
    ProfileError error() const { return error_; }
    bool ready() const { return initialized_; }
    void initializeOff() override;
    bool buttonPressed();
    void setFan(bool on) override;
    void setIndicator(bool on) override;
private:
    GpioPort& gpio_;
    BoardProfile profile_;
    ProfileError error_;
    bool initialized_ = false;
};

} // namespace esp32fan
