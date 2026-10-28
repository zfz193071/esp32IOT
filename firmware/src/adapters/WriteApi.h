#pragma once

#include "StateApi.h"
#include <vector>

namespace esp32fan {

// Populated only after bounded HTTP parsing. Duplicate Host/Origin/Content-Type
// or ambiguous framing must set ambiguousHeaders or be rejected by transport.
struct WriteRequest {
    std::string method;
    std::string path;
    std::string host;
    bool hasOrigin = false;
    std::string origin;
    std::string contentType;
    std::string body;
    bool ambiguousHeaders = false;
};

struct WritePolicy {
    // Trusted exact HTTP origins from device config, never from request Host.
    // Examples: http://192.168.1.20 or http://fan.local (no trailing slash).
    std::vector<std::string> allowedOrigins;
    bool allowOriginlessDiagnostics = false;
};

StateResponse handleWriteRequest(const WriteRequest& request, const WritePolicy& policy,
                                 FanController& controller, ConfigStore& store);

} // namespace esp32fan
