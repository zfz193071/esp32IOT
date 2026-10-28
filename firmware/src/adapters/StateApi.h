#pragma once

#include "domain/FanController.h"
#include <string>

namespace esp32fan {

struct StateResponse {
    int status = 200;
    const char* contentType = "application/json; charset=utf-8";
    const char* cacheControl = "no-store";
    const char* allow = nullptr;
    std::string body;
};

// Transport-independent GET endpoint. The HTTP adapter passes a parsed path,
// without query or fragment, and must enforce wire-size/time limits itself.
StateResponse handleStateRequest(const std::string& method, const std::string& path,
                                const DeviceState& state);

} // namespace esp32fan
