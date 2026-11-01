#include "SocketHttpTransport.h"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <lwip/sockets.h>
#include <lwip/inet.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace esp32fan {
SocketHttpTransport::SocketHttpTransport(SocketCloser& closer) : closer_(closer) {}
SocketHttpTransport::~SocketHttpTransport() { end(); }

bool SocketHttpTransport::configure(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        lastError_ = errno;
        return false;
    }
#ifdef SO_NOSIGPIPE
    // macOS/BSD has per-socket SIGPIPE suppression instead of MSG_NOSIGNAL.
    const int enabled = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0) {
        lastError_ = errno;
        return false;
    }
#endif
    return true;
}

void SocketHttpTransport::releaseRetired() {
    if (client_ >= 0 && clientRetired_ && closer_.release(client_)) client_ = -1;
    if (listener_ >= 0 && listenerRetired_ && closer_.release(listener_)) listener_ = -1;
}

bool SocketHttpTransport::begin(const char* address, std::uint16_t port) {
    releaseRetired();
    if (listener_ >= 0 || client_ >= 0 || !closer_.idle()) { lastError_ = EBUSY; return false; }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    // inet_pton only: never block on name resolution or expose every interface.
    if (!address || ::inet_pton(AF_INET, address, &local.sin_addr) != 1 ||
        ntohl(local.sin_addr.s_addr) == 0 || (ntohl(local.sin_addr.s_addr) >> 24) >= 224) {
        lastError_ = EINVAL;
        return false;
    }
    listenerRetired_ = false;
    listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ < 0) { lastError_ = errno; return false; }
    const int reuse = 1;
    if (!configure(listener_)) { end(); return false; }
    if (::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
        ::bind(listener_, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) < 0 ||
        ::listen(listener_, 2) < 0) {
        lastError_ = errno;
        end();
        return false;
    }
    socklen_t length = sizeof(local);
    if (::getsockname(listener_, reinterpret_cast<sockaddr*>(&local), &length) < 0) {
        lastError_ = errno;
        end();
        return false;
    }
    port_ = ntohs(local.sin_port);
    lastError_ = 0;
    return true;
}

void SocketHttpTransport::end() {
    clientRetired_ = listenerRetired_ = true;
    port_ = 0;
    releaseRetired();
}

std::uint16_t SocketHttpTransport::port() const { return port_; }
int SocketHttpTransport::lastError() const { return lastError_; }

bool SocketHttpTransport::accept() {
    releaseRetired();
    if (listener_ < 0 || listenerRetired_ || client_ >= 0 || !closer_.idle()) return false;
    client_ = ::accept(listener_, nullptr, nullptr);
    if (client_ < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) lastError_ = errno;
        return false;
    }
    clientRetired_ = false;
    // Accepted sockets need their own nonblocking flag on both POSIX and lwIP.
    if (!configure(client_)) { close(); return false; }
    lastError_ = 0;
    return true;
}

HttpIoResult SocketHttpTransport::ioResult(long result, bool reading) {
    if (result > 0) return {HttpIoStatus::Progress, static_cast<std::size_t>(result)};
    if (result == 0) return {reading ? HttpIoStatus::End : HttpIoStatus::Error, 0};
    const int error = errno;
    if (error == EAGAIN || error == EWOULDBLOCK || error == EINTR)
        return {HttpIoStatus::WouldBlock, 0}; // retry next cycle, never spin here
    lastError_ = error;
    return {HttpIoStatus::Error, 0};
}

HttpIoResult SocketHttpTransport::receive(char* bytes, std::size_t capacity) {
    if (client_ < 0 || clientRetired_ || !bytes || capacity == 0) return {HttpIoStatus::Error, 0};
    return ioResult(::recv(client_, bytes,
        std::min(capacity, static_cast<std::size_t>(HttpApiPort::maxIoBytes)), 0), true);
}

HttpIoResult SocketHttpTransport::send(const char* bytes, std::size_t size) {
    if (client_ < 0 || clientRetired_ || !bytes || size == 0) return {HttpIoStatus::Error, 0};
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0; // ESP lwIP does not deliver Unix SIGPIPE
#endif
    return ioResult(::send(client_, bytes,
        std::min(size, static_cast<std::size_t>(HttpApiPort::maxIoBytes)), flags), false);
}

void SocketHttpTransport::close() {
    clientRetired_ = true;
    releaseRetired();
}
} // namespace esp32fan
