#include "board/Esp32SerialSink.h"
#include <atomic>
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
static std::thread::id mainThread;
static std::mutex serialMutex;
static std::condition_variable serialChanged;
static bool holdWrite = false, shortWrite = false;
static std::atomic<unsigned> writeCalls{0};
static std::string transmitted;
std::size_t HardwareSerial::write(const std::uint8_t* data, std::size_t length) {
    check(std::this_thread::get_id() != mainThread, "UART writes never execute on control thread");
    ++writeCalls;
    std::unique_lock<std::mutex> lock(serialMutex);
    serialChanged.wait(lock, [] { return !holdWrite; });
    const auto sent = shortWrite ? length - 1 : length;
    transmitted.append(reinterpret_cast<const char*>(data), sent);
    return sent;
}
static void releaseWriter() {
    { std::lock_guard<std::mutex> lock(serialMutex); holdWrite = false; }
    serialChanged.notify_all();
}
static void awaitWrite(unsigned count) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (writeCalls < count && std::chrono::steady_clock::now() < until) std::this_thread::yield();
    check(writeCalls >= count, "worker reaches UART");
}
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
    check(stack == 3072 && priority == 1, "one fixed low-priority serial worker");
    if (failTask) return 0;
    *handle = context;
    workers.emplace_back(task, context);
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { check(!task, "worker deletes itself after shutdown acknowledgement"); }
static void joinWorkers() { for (auto& worker : workers) worker.join(); workers.clear(); }
int main() {
    mainThread = std::this_thread::get_id();
    HardwareSerial serial;
    for (unsigned failure = 1; failure <= 3; ++failure) {
        esp32fan::Esp32SerialSink sink(serial);
        check(!sink.tryWriteLine("before start"), "unstarted sink rejects lines");
        queueAllocations = 0; failAllocation = failure < 3 ? failure : 0; failTask = failure == 3;
        check(!sink.start() && liveQueues == 0 && !sink.tryWriteLine("failed start"),
              "allocation failure releases resources and rejects lines");
        failAllocation = 0; failTask = false;
        check(sink.start() && sink.start(), "startup retry and idempotent start");
    }
    joinWorkers();
    check(liveQueues == 0, "empty teardown releases queues");
    {
        esp32fan::Esp32SerialSink sink(serial);
        check(sink.start(), "worker starts");
        check(!sink.tryWriteLine(std::string(513, 'x')) && !sink.tryWriteLine("bad\nline") &&
              !sink.tryWriteLine("bad\rline") && !sink.tryWriteLine(std::string("a\0b", 3)),
              "oversize and framing injection rejected even when queue is empty");
        { std::lock_guard<std::mutex> lock(serialMutex); holdWrite = true; }
        check(sink.tryWriteLine("first"), "first record accepted");
        awaitWrite(1);
        const unsigned allocations = queueAllocations;
        std::string owned = "owned";
        check(sink.tryWriteLine(owned), "queue owns a copy");
        owned.assign("mutated");
        check(sink.tryWriteLine(std::string(512, 'x')) && sink.tryWriteLine("") && sink.tryWriteLine("last"),
              "four queued records plus one in-flight record");
        esp32fan::TextDiagnostics diagnostics(sink);
        for (unsigned i = 0; i < 10000; ++i)
            diagnostics.temperature(esp32fan::TemperatureDiagnostic::ReadFailed);
        check(diagnostics.droppedLines() == 10000 && queueAllocations == allocations,
              "stalled UART cannot block producer or grow resources; drops counted");
        releaseWriter();
        // Destruction drains the accepted queue; no test sleeps required.
    }
    joinWorkers();
    check(transmitted == "first\nowned\n" + std::string(512, 'x') + "\n\nlast\n",
          "FIFO, owned bytes, exact length and single newline preserved");
    check(liveQueues == 0 && writeCalls == 5, "all accepted records transmitted once and resources freed");
    {
        esp32fan::Esp32SerialSink sink(serial);
        check(sink.start(), "failure test worker starts");
        { std::lock_guard<std::mutex> lock(serialMutex); holdWrite = true; shortWrite = true; }
        check(sink.tryWriteLine("partial"), "partial record accepted");
        awaitWrite(6);
        check(sink.tryWriteLine("must not follow partial"), "record accepted before failure observation");
        releaseWriter();
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!sink.writeFailed() && std::chrono::steady_clock::now() < until) std::this_thread::yield();
        check(sink.writeFailed() && !sink.tryWriteLine("later") && !sink.start(),
              "short write latches failure and rejects later admission without retry");
    }
    joinWorkers();
    check(writeCalls == 6 && liveQueues == 0, "queued records after short write discarded; shutdown still completes");
    transmitted.clear();
    shortWrite = false;
    {
        esp32fan::Esp32SerialSink sink(serial);
        check(sink.start(), "new sink instance may initialize after old instance is destroyed");
        esp32fan::TextDiagnostics diagnostics(sink);
        esp32fan::DeviceState state;
        state.bootId.assign(1024, 'b');
        state.revision = 18446744073709551615ULL;
        state.fault = esp32fan::Fault::TemperatureUnavailable;
        state.configSaveBlocked = true;
        diagnostics.startup(esp32fan::LoadResult::ReadFailureDefaults, state);
        check(diagnostics.droppedLines() == 0, "real startup formatting fits fixed line budget");
    }
    joinWorkers();
    check(transmitted.find("startup config=read_failure_defaults boot=" + std::string(128, 'b')) == 0 &&
          transmitted.find("config_save_blocked=true\n") != std::string::npos && transmitted.size() <= 513,
          "real diagnostics retain bounded identifiers and uncertain-save information");
    std::cout << "PASS: bounded serial diagnostics worker (host UART/FreeRTOS stubs, no hardware timing validation)\n";
}
