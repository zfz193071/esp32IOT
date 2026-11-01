#pragma once

#include "adapters/HttpApiPort.h"

namespace esp32fan {

// Separates potentially blocking socket destruction from the control loop.
// Dedicated to one transport with capacity for both listener and client.
// Successful release transfers exclusive fd ownership. idle must be nonblocking.
class SocketCloser {
public:
    virtual ~SocketCloser() = default;
    virtual bool release(int fd) = 0;
    virtual bool idle() = 0;
};

// BSD sockets shared by ESP-IDF/lwIP and the host TCP regression.
// One listener + one client, backlog 2. No implicit wildcard bind or DNS lookup.
class SocketHttpTransport final : public HttpTransport {
public:
    explicit SocketHttpTransport(SocketCloser& closer);
    ~SocketHttpTransport() override;
    SocketHttpTransport(const SocketHttpTransport&) = delete;
    SocketHttpTransport& operator=(const SocketHttpTransport&) = delete;
    // Call only once network has a numeric IPv4 address. Port 0 selects an
    // ephemeral port for host tests; device assembly should explicitly use 80.
    bool begin(const char* address, std::uint16_t port);
    void end(); // stop listener and client; closure may finish asynchronously
    std::uint16_t port() const;
    int lastError() const; // errno, never request data or credentials
    bool accept() override;
    HttpIoResult receive(char* bytes, std::size_t capacity) override;
    HttpIoResult send(const char* bytes, std::size_t size) override;
    void close() override;
private:
    bool configure(int fd);
    void releaseRetired();
    HttpIoResult ioResult(long result, bool reading);
    SocketCloser& closer_;
    int listener_ = -1;
    int client_ = -1;
    bool clientRetired_ = false;
    bool listenerRetired_ = false;
    std::uint16_t port_ = 0;
    int lastError_ = 0;
};
} // namespace esp32fan
