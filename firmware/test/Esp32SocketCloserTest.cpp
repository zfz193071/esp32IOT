#include "board/Esp32SocketCloser.h"
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

static void check(bool ok, const char* rule) {
    if (!ok) { std::cerr << "FAIL: " << rule << '\n'; std::exit(1); }
}
static std::mutex closeMutex;
static std::condition_variable closeChanged;
static bool holdClose = false;
static std::atomic<unsigned> closeCalls{0};
static std::atomic<int> closeError{0};
static std::thread::id mainThread;
static int testClose(int) {
    check(std::this_thread::get_id() != mainThread, "close never runs on the control thread");
    ++closeCalls;
    std::unique_lock<std::mutex> lock(closeMutex);
    closeChanged.wait(lock, [] { return !holdClose; });
    errno = closeError.load();
    return errno ? -1 : 0;
}
// Fault injection only in this translation unit; production calls unistd close.
#define close testClose
#include "../src/board/Esp32SocketCloser.cpp"
#undef close

struct Queue {
    unsigned capacity, size;
    std::deque<std::vector<unsigned char>> items;
    std::mutex mutex;
    std::condition_variable changed;
    Queue(unsigned count, unsigned bytes) : capacity(count), size(bytes) {}
};
static unsigned queueAllocations = 0, liveQueues = 0, failAllocation = 0;
static bool failTask = false;
static std::vector<std::thread> workers;
QueueHandle_t xQueueCreate(UBaseType_t count, UBaseType_t size) {
    if (++queueAllocations == failAllocation) return nullptr;
    ++liveQueues;
    return new Queue(count, size);
}
BaseType_t xQueueSend(QueueHandle_t handle, const void* item, TickType_t wait) {
    auto& queue = *static_cast<Queue*>(handle);
    std::unique_lock<std::mutex> lock(queue.mutex);
    if (!wait && queue.items.size() == queue.capacity) return 0;
    if (wait) queue.changed.wait(lock, [&] { return queue.items.size() < queue.capacity; });
    const auto* bytes = static_cast<const unsigned char*>(item);
    queue.items.emplace_back(bytes, bytes + queue.size);
    queue.changed.notify_all();
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t handle, void* item, TickType_t wait) {
    auto& queue = *static_cast<Queue*>(handle);
    std::unique_lock<std::mutex> lock(queue.mutex);
    if (!wait && queue.items.empty()) return 0;
    if (wait) queue.changed.wait(lock, [&] { return !queue.items.empty(); });
    std::memcpy(item, queue.items.front().data(), queue.size);
    queue.items.pop_front();
    queue.changed.notify_all();
    return pdTRUE;
}
void vQueueDelete(QueueHandle_t queue) { delete static_cast<Queue*>(queue); --liveQueues; }
BaseType_t xTaskCreate(void (*task)(void*), const char*, std::uint32_t stack,
                       void* context, UBaseType_t priority, TaskHandle_t* handle) {
    check(stack == 3072 && priority == 1, "one fixed low-priority close worker");
    if (failTask) return 0;
    *handle = context;
    workers.emplace_back(task, context);
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { check(!task, "worker deletes itself after shutdown acknowledgement"); }
static void joinWorkers() { for (auto& worker : workers) worker.join(); workers.clear(); }
static void waitIdle(esp32fan::Esp32SocketCloser& closer) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!closer.idle() && std::chrono::steady_clock::now() < until) std::this_thread::yield();
    check(closer.idle(), "completed closures free the bounded slots");
}
int main() {
    mainThread = std::this_thread::get_id();
    for (unsigned failure = 1; failure <= 3; ++failure) {
        esp32fan::Esp32SocketCloser closer;
        queueAllocations = 0; failAllocation = failure < 3 ? failure : 0; failTask = failure == 3;
        check(!closer.start() && liveQueues == 0 && !closer.release(10),
              "allocation failure cleans resources and refuses ownership");
        failAllocation = 0; failTask = false;
        check(closer.start() && closer.start() && closer.idle(), "failed setup can retry and successful start is idempotent");
    }
    joinWorkers();
    check(liveQueues == 0, "setup/shutdown releases both queues");
    {
        esp32fan::Esp32SocketCloser closer;
        check(closer.start(), "close worker starts");
        { std::lock_guard<std::mutex> lock(closeMutex); holdClose = true; }
        check(closer.release(10) && closer.release(11), "two descriptors transfer ownership without waiting");
        for (unsigned i = 0; i < 1000; ++i)
            check(!closer.idle() && !closer.release(12), "stalled close cannot grow the queue or block the main loop");
        { std::lock_guard<std::mutex> lock(closeMutex); holdClose = false; }
        closeChanged.notify_all();
        waitIdle(closer);
        check(closeCalls == 2, "each descriptor closed exactly once");
        closeError = EIO;
        check(closer.release(13), "faulted close is dispatched");
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!closer.lastError() && std::chrono::steady_clock::now() < until) std::this_thread::yield();
        check(closer.lastError() == EIO && !closer.idle() && !closer.start(),
              "close failure latches diagnostics and prevents new connections");
        check(closer.release(14), "remaining listener may still be retired after a close failure");
    }
    joinWorkers();
    check(liveQueues == 0 && closeCalls == 4, "shutdown drains pending jobs before freeing queues");
    std::cout << "PASS: bounded ESP32 close worker (host FreeRTOS/thread stub, no SDK timing validation)\n";
}
