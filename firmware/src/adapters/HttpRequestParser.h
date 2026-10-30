#pragma once

#include "WriteApi.h"
#include <cstddef>
#include <cstdint>

namespace esp32fan {

enum class HttpParseStatus { Receiving, Complete, Rejected };

// Bounded HTTP/1.1 request subset for direct browser-to-device traffic.
// One request per connection. The transport must close after its response and
// discard any unconsumed bytes; never reset this parser on the same connection.
class HttpRequestParser {
public:
    enum : std::size_t { maxBytesPerFeed = 256, maxHeaderBytes = 2048,
                        maxLineBytes = 512, maxBodyBytes = 1024 };
    enum : std::uint32_t { receiveTimeoutMs = 2000 };

    explicit HttpRequestParser(std::uint32_t acceptedAtMs);
    // Returns bytes consumed, capped at maxBytesPerFeed. Call at most once per
    // main-loop cycle; leave the remainder for later cycles. Does not read I/O.
    std::size_t feed(const char* bytes, std::size_t size, std::uint32_t nowMs);
    // Call on every receiving cycle, including when no socket bytes arrive.
    void tick(std::uint32_t nowMs);
    void endOfInput();
    HttpParseStatus status() const;
    // Zero unless rejected. Transport owns error response and connection close.
    int errorStatus() const;
    // Only a complete request is exposed, once. Failure preserves destination.
    bool takeRequest(WriteRequest& output);

private:
    enum class Phase { RequestLine, Headers, Body };
    void reject(int status);
    void receiveByte(unsigned char byte);
    void parseRequestLine();
    void parseHeader();
    void finishHeaders();

    std::uint32_t acceptedAtMs_;
    HttpParseStatus status_ = HttpParseStatus::Receiving;
    Phase phase_ = Phase::RequestLine;
    int errorStatus_ = 0;
    std::string line_;
    WriteRequest request_;
    std::size_t headerBytes_ = 0;
    std::size_t bodyLength_ = 0;
    bool sawCr_ = false;
    bool hasHost_ = false;
    bool hasContentType_ = false;
    bool hasLength_ = false;
    bool taken_ = false;
};

} // namespace esp32fan
