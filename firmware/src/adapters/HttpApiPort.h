#pragma once

#include "HttpRequestParser.h"
#include "app/AppLoop.h"

namespace esp32fan {

enum class HttpIoStatus { Progress, WouldBlock, End, Error };
struct HttpIoResult {
    HttpIoStatus status;
    std::size_t size;
};

// Owns a listener and at most one accepted client. All operations must return
// immediately; no hidden retries, flush waits or unbounded application queues.
class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual bool accept() = 0; // false: no client accepted, including listener errors
    // Progress must report 1..capacity bytes. End means peer write-side EOF.
    virtual HttpIoResult receive(char* bytes, std::size_t capacity) = 0;
    // Must copy/consume bytes before returning; never retain the supplied pointer.
    virtual HttpIoResult send(const char* bytes, std::size_t size) = 0;
    // Release only the accepted client, without waiting for peer acknowledgement.
    virtual void close() = 0;
};

// Listener lifecycle for a network-managed HTTP service. begin/end must not
// wait for a peer or socket destruction; end is safe after a failed begin.
class HttpListener : public HttpTransport {
public:
    virtual bool begin(const char* address, std::uint16_t port) = 0;
    virtual void end() = 0;
};

// Single-threaded ApiPort: call poll(nowMs) once before each AppLoop::step.
// sendResponse queues only; the next poll sends at most one bounded chunk.
class HttpApiPort : public ApiPort {
public:
    enum : std::size_t { maxIoBytes = 256, maxResponseBodyBytes = 2048 };
    enum : std::uint32_t { responseTimeoutMs = 5000 };
    explicit HttpApiPort(HttpTransport& transport);
    ~HttpApiPort() override;
    HttpApiPort(const HttpApiPort&) = delete;
    HttpApiPort& operator=(const HttpApiPort&) = delete;
    void poll(std::uint32_t nowMs);
    void stop(); // discard queued work on network shutdown; listener is caller-owned
    bool takeRequest(WriteRequest& request) override;
    void sendResponse(const StateResponse& response) override;
private:
    enum class Phase { Idle, Receiving, Ready, AwaitResponse, Sending };
    void route(std::uint32_t nowMs);
    void queueError(int status, const char* allow = nullptr);
    void queueHeaders(int status, const char* type, const char* cache,
                      const char* allow, std::size_t length);
    void sendChunk();

    HttpTransport& transport_;
    HttpRequestParser parser_{0};
    Phase phase_ = Phase::Idle;
    WriteRequest request_;
    bool head_ = false;
    std::uint32_t responseAtMs_ = 0;
    std::string headers_;
    std::string body_;
    const unsigned char* assetData_ = nullptr;
    std::size_t bodySize_ = 0;
    std::size_t headerOffset_ = 0;
    std::size_t bodyOffset_ = 0;
};

} // namespace esp32fan
