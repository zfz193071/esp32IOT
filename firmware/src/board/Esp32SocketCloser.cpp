#include "Esp32SocketCloser.h"
#include <cerrno>
#include <unistd.h>

namespace esp32fan {
bool Esp32SocketCloser::start() {
    if (worker_) return lastError() == 0;
    jobs_ = xQueueCreate(2, sizeof(int));
    done_ = xQueueCreate(2, sizeof(Completion));
    if (jobs_ && done_ && xTaskCreate(run, "http-close", 3072, this, 1, &worker_) == pdPASS)
        return true;
    if (jobs_) vQueueDelete(jobs_);
    if (done_) vQueueDelete(done_);
    jobs_ = done_ = nullptr;
    worker_ = nullptr;
    return false;
}

void Esp32SocketCloser::run(void* context) {
    auto* self = static_cast<Esp32SocketCloser*>(context);
    // Copy handles before acknowledging shutdown; never access self afterward.
    const auto jobs = self->jobs_;
    const auto done = self->done_;
    for (;;) {
        int fd = -1;
        xQueueReceive(jobs, &fd, portMAX_DELAY);
        if (fd < 0) {
            const Completion finished{0};
            xQueueSend(done, &finished, portMAX_DELAY);
            vTaskDelete(nullptr);
            return;
        }
        const int result = ::close(fd);
        const Completion finished{result == 0 ? 0 : errno};
        xQueueSend(done, &finished, portMAX_DELAY);
    }
}

void Esp32SocketCloser::collect() {
    if (!done_) return;
    Completion result{};
    for (unsigned i = 0; i < 2; ++i) {
        if (xQueueReceive(done_, &result, 0) != pdTRUE) break;
        --pending_;
        if (result.error != 0) lastError_ = result.error;
    }
}

bool Esp32SocketCloser::release(int fd) {
    collect();
    if (!worker_ || fd < 0 || pending_ >= 2) return false;
    if (xQueueSend(jobs_, &fd, 0) != pdTRUE) return false;
    ++pending_;
    return true;
}

bool Esp32SocketCloser::idle() { collect(); return worker_ && pending_ == 0 && lastError_ == 0; }
int Esp32SocketCloser::lastError() { collect(); return lastError_; }

Esp32SocketCloser::~Esp32SocketCloser() {
    if (!worker_) return;
    // Teardown only, outside the control loop, after transport destruction.
    // Runtime disconnect uses transport.end() and never waits for these results.
    Completion result{};
    while (pending_ > 0) {
        xQueueReceive(done_, &result, portMAX_DELAY);
        --pending_;
    }
    const int stop = -1;
    xQueueSend(jobs_, &stop, portMAX_DELAY);
    xQueueReceive(done_, &result, portMAX_DELAY);
    vQueueDelete(jobs_);
    vQueueDelete(done_);
}
} // namespace esp32fan
