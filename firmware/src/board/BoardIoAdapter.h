#pragma once

#include "app/AppLoop.h"
#include "board/BoardProfile.h"

namespace esp32fan {

class GpioPort {
public:
    virtual ~GpioPort() = default;
    // The implementation must establish initialHigh before enabling output,
    // without an opposite-level pulse during mode change.
    virtual void configureOutput(int gpio, bool initialHigh) = 0;
    virtual void configureInput(int gpio, bool pullUp) = 0;
    virtual bool read(int gpio) = 0;
    virtual void write(int gpio, bool high) = 0;
};

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
