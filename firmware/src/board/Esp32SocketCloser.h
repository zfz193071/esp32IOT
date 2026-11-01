#pragma once

#include "SocketHttpTransport.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace esp32fan {
// Dedicated to one SocketHttpTransport. At most two outstanding descriptors.
// Start during setup, after outputs are OFF and before opening the listener.
class Esp32SocketCloser final : public SocketCloser {
public:
    Esp32SocketCloser() = default;
    ~Esp32SocketCloser() override;
    Esp32SocketCloser(const Esp32SocketCloser&) = delete;
    Esp32SocketCloser& operator=(const Esp32SocketCloser&) = delete;
    bool start();
    bool release(int fd) override;
    bool idle() override;
    int lastError(); // close failure latches: reject further listener/client creation
private:
    struct Completion { int error; };
    static void run(void* context);
    void collect();
    QueueHandle_t jobs_ = nullptr;
    QueueHandle_t done_ = nullptr;
    TaskHandle_t worker_ = nullptr;
    unsigned pending_ = 0;
    int lastError_ = 0;
};
} // namespace esp32fan
