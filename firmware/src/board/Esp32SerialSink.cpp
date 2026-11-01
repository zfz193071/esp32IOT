#include "Esp32SerialSink.h"
#include <cstring>

namespace esp32fan {
bool Esp32SerialSink::start() {
    if (worker_) return !writeFailed();
    lines_ = xQueueCreate(queuedLines, sizeof(Line));
    stopped_ = xQueueCreate(1, sizeof(bool));
    if (lines_ && stopped_ && xTaskCreate(run, "diagnostic-tx", 3072, this, 1, &worker_) == pdPASS)
        return true;
    if (lines_) vQueueDelete(lines_);
    if (stopped_) vQueueDelete(stopped_);
    lines_ = stopped_ = nullptr;
    worker_ = nullptr;
    return false;
}

bool Esp32SerialSink::tryWriteLine(const std::string& line) {
    if (!worker_ || writeFailed() || line.size() > maxLineBytes ||
        line.find_first_of("\r\n") != std::string::npos || line.find('\0') != std::string::npos) return false;
    Line record{};
    record.size = static_cast<std::uint16_t>(line.size() + 1);
    std::memcpy(record.bytes, line.data(), line.size());
    record.bytes[line.size()] = '\n';
    return xQueueSend(lines_, &record, 0) == pdTRUE;
}

void Esp32SerialSink::run(void* context) {
    auto* self = static_cast<Esp32SerialSink*>(context);
    const auto lines = self->lines_;
    const auto stopped = self->stopped_;
    for (;;) {
        Line line{};
        xQueueReceive(lines, &line, portMAX_DELAY);
        if (line.size == 0) {
            const bool done = true;
            xQueueSend(stopped, &done, portMAX_DELAY);
            // The owner may be destroyed after the acknowledgement.
            vTaskDelete(nullptr);
            return;
        }
        // A short write can leave a partial line on the wire. Do not append
        // later records to it or retry an ambiguous transmission.
        if (!self->writeFailed() && self->serial_.write(line.bytes, line.size) != line.size)
            self->failed_.store(true);
    }
}

Esp32SerialSink::~Esp32SerialSink() {
    if (!worker_) return;
    // Drains accepted records; may wait for the UART. Never call in AppLoop.
    const Line stop{};
    xQueueSend(lines_, &stop, portMAX_DELAY);
    bool done = false;
    xQueueReceive(stopped_, &done, portMAX_DELAY);
    vQueueDelete(lines_);
    vQueueDelete(stopped_);
}
} // namespace esp32fan
