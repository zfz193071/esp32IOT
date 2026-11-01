#pragma once

#include "adapters/TextDiagnostics.h"
#include <HardwareSerial.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace esp32fan {
// One control-loop producer; exclusive writer to an already initialized UART.
// UART lifetime must exceed this object. Start/teardown only outside AppLoop.
class Esp32SerialSink final : public DiagnosticSink {
public:
    enum : unsigned { maxLineBytes = 512, queuedLines = 4 };
    explicit Esp32SerialSink(HardwareSerial& serial) : serial_(serial) {}
    ~Esp32SerialSink() override;
    Esp32SerialSink(const Esp32SerialSink&) = delete;
    Esp32SerialSink& operator=(const Esp32SerialSink&) = delete;
    bool start();
    bool tryWriteLine(const std::string& line) override;
    bool writeFailed() const { return failed_.load(); }
private:
    struct Line { std::uint16_t size; std::uint8_t bytes[maxLineBytes + 1]; };
    static void run(void* context);
    HardwareSerial& serial_;
    QueueHandle_t lines_ = nullptr;
    QueueHandle_t stopped_ = nullptr;
    TaskHandle_t worker_ = nullptr;
    std::atomic<bool> failed_{false};
};
} // namespace esp32fan
