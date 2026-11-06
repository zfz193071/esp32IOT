#pragma once

namespace esp32fan {
class GpioPort {
public:
    virtual ~GpioPort() = default;
    // Establish initialHigh before enabling output, without an opposite pulse.
    virtual void configureOutput(int gpio, bool initialHigh) = 0;
    virtual void configureInput(int gpio, bool pullUp) = 0;
    virtual bool read(int gpio) = 0;
    virtual void write(int gpio, bool high) = 0;
};
} // namespace esp32fan
