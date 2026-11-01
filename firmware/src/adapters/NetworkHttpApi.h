#pragma once

#include "HttpApiPort.h"

namespace esp32fan {

// Owns HTTP lifecycle through one exclusive listener. Pass this ApiPort to
// AppLoop; do not separately poll HTTP or update NetworkService in board code.
class NetworkHttpApi final : public ApiPort {
public:
    enum : std::uint32_t { retryDelayMs = 5000 };
    explicit NetworkHttpApi(HttpListener& listener, std::uint16_t port = 80);
    ~NetworkHttpApi() override;
    NetworkHttpApi(const NetworkHttpApi&) = delete;
    NetworkHttpApi& operator=(const NetworkHttpApi&) = delete;
    void update(std::uint32_t nowMs, const NetworkUpdate& network) override;
    bool takeRequest(WriteRequest& request) override;
    void sendResponse(const StateResponse& response) override;
    bool listening() const;
private:
    HttpListener& listener_;
    HttpApiPort http_;
    const std::uint16_t port_;
    std::string address_;
    bool listening_ = false;
    bool attempted_ = false;
    std::uint32_t attemptedAtMs_ = 0;
};
} // namespace esp32fan
