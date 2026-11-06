#pragma once

#include "GpioPort.h"

namespace esp32fan {

// Concrete Arduino-ESP32 GPIO calls. The selected BoardProfile remains external.
class ArduinoGpioPort : public GpioPort {
public:
    void configureOutput(int gpio, bool initialHigh) override;
    void configureInput(int gpio, bool pullUp) override;
    bool read(int gpio) override;
    void write(int gpio, bool high) override;
};

} // namespace esp32fan
