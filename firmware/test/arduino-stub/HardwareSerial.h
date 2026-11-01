#pragma once
#include <cstddef>
#include <cstdint>

class HardwareSerial {
public:
    std::size_t write(const std::uint8_t* data, std::size_t length);
};
