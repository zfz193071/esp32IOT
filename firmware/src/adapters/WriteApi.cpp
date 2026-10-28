#include "WriteApi.h"

#include <cmath>
#include <locale>
#include <sstream>

namespace esp32fan {
namespace {
StateResponse error(int status, const char* code, const char* message) {
    StateResponse response;
    response.status = status;
    response.body = std::string("{\"error\":{\"code\":\"") + code +
        "\",\"message\":\"" + message + "\"}}";
    return response;
}

bool digit(char c) { return c >= '0' && c <= '9'; }
int hex(char c) {
    if (digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Bounded parser for this API's flat, ASCII-named command objects only.
// No arrays, nested objects, nulls or unknown fields belong to the contract.
class CommandJson {
public:
    explicit CommandJson(const std::string& body) : body_(body) {}
    bool parse(const std::string& path) {
        if (!take('{')) return false;
        unsigned seen = 0;
        do {
            std::string key;
            if (!string(key) || !take(':')) return false;
            unsigned field = 0;
            if (path == "/api/v1/power" && key == "on") {
                field = 1;
                if (!boolean(on)) return false;
            } else if (path == "/api/v1/mode" && key == "mode") {
                field = 1;
                if (!string(mode) || (mode != "manual" && mode != "auto")) return false;
            } else if (path == "/api/v1/config" && key == "onThresholdC") {
                field = 1;
                if (!number(config.onThresholdC)) return false;
            } else if (path == "/api/v1/config" && key == "offThresholdC") {
                field = 2;
                if (!number(config.offThresholdC)) return false;
            } else return false;
            if (seen & field) return false;
            seen |= field;
            if (take('}')) {
                whitespace();
                return position_ == body_.size() && seen == (path == "/api/v1/config" ? 3u : 1u);
            }
        } while (take(','));
        return false;
    }
    bool on = false;
    std::string mode;
    TemperatureConfig config;
private:
    void whitespace() {
        while (position_ < body_.size() && (body_[position_] == ' ' || body_[position_] == '\t' ||
               body_[position_] == '\n' || body_[position_] == '\r')) ++position_;
    }
    bool take(char c) {
        whitespace();
        if (position_ == body_.size() || body_[position_] != c) return false;
        ++position_;
        return true;
    }
    bool string(std::string& value) {
        if (!take('"')) return false;
        while (position_ < body_.size()) {
            unsigned char c = body_[position_++];
            if (c == '"') return true;
            if (c < 0x20 || c > 0x7f) return false;
            if (c == '\\') {
                if (position_ == body_.size()) return false;
                c = body_[position_++];
                switch (c) {
                case '"': case '\\': case '/': break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (position_ == body_.size()) return false;
                        const int nibble = hex(body_[position_++]);
                        if (nibble < 0) return false;
                        code = code * 16 + static_cast<unsigned>(nibble);
                    }
                    if (code > 0x7f) return false;
                    c = static_cast<unsigned char>(code);
                    break;
                }
                default: return false;
                }
            }
            value += static_cast<char>(c);
            if (value.size() > 64) return false;
        }
        return false;
    }
    bool boolean(bool& value) {
        whitespace();
        if (body_.compare(position_, 4, "true") == 0) { position_ += 4; value = true; return true; }
        if (body_.compare(position_, 5, "false") == 0) { position_ += 5; value = false; return true; }
        return false;
    }
    bool number(double& value) {
        whitespace();
        const auto start = position_;
        if (position_ < body_.size() && body_[position_] == '-') ++position_;
        if (position_ == body_.size() || !digit(body_[position_])) return false;
        if (body_[position_] == '0') ++position_;
        else while (position_ < body_.size() && digit(body_[position_])) ++position_;
        if (position_ < body_.size() && body_[position_] == '.') {
            ++position_;
            const auto decimal = position_;
            while (position_ < body_.size() && digit(body_[position_])) ++position_;
            if (decimal == position_) return false;
        }
        if (position_ < body_.size() && (body_[position_] == 'e' || body_[position_] == 'E')) {
            ++position_;
            if (position_ < body_.size() && (body_[position_] == '+' || body_[position_] == '-')) ++position_;
            const auto exponent = position_;
            while (position_ < body_.size() && digit(body_[position_])) ++position_;
            if (exponent == position_) return false;
        }
        if (position_ - start > 64) return false;
        std::istringstream input(body_.substr(start, position_ - start));
        input.imbue(std::locale::classic());
        input >> value;
        return !input.fail() && std::isfinite(value);
    }
    const std::string& body_;
    std::size_t position_ = 0;
};

bool permitted(const WriteRequest& request, const WritePolicy& policy) {
    if (request.host.empty() || request.host.size() > 128 || request.origin.size() > 256) return false;
    for (const auto& origin : policy.allowedOrigins) {
        if (origin.compare(0, 7, "http://") != 0) continue;
        const auto authority = origin.substr(7);
        if (authority.empty() || authority.find_first_of("/\\?#@ \t\r\n") != std::string::npos) continue;
        if (request.host != authority) continue;
        if (request.hasOrigin) return request.origin == origin;
        return policy.allowOriginlessDiagnostics;
    }
    return false;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    return value.substr(first, value.find_last_not_of(" \t") - first + 1);
}

bool jsonContentType(std::string value) {
    if (value.size() > 128) return false;
    for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    const auto separator = value.find(';');
    if (trim(value.substr(0, separator)) != "application/json") return false;
    if (separator == std::string::npos) return true;
    const auto parameter = trim(value.substr(separator + 1));
    const auto equal = parameter.find('=');
    return equal != std::string::npos && trim(parameter.substr(0, equal)) == "charset" &&
        trim(parameter.substr(equal + 1)) == "utf-8";
}

} // namespace

StateResponse handleWriteRequest(const WriteRequest& request, const WritePolicy& policy,
                                 FanController& controller, ConfigStore& store) {
    if (request.path.size() > 128) return error(414, "PATH_TOO_LONG", "Path too long");
    if (request.path != "/api/v1/power" && request.path != "/api/v1/mode" && request.path != "/api/v1/config")
        return error(404, "NOT_FOUND", "Route not found");
    if (request.method != "PUT") {
        auto response = error(405, "METHOD_NOT_ALLOWED", "Use PUT");
        response.allow = "PUT";
        return response;
    }
    if (request.ambiguousHeaders) return error(400, "INVALID_REQUEST", "Ambiguous headers");
    if (!permitted(request, policy)) return error(403, "FORBIDDEN", "Origin not allowed");
    if (!jsonContentType(request.contentType)) return error(400, "INVALID_REQUEST", "JSON required");
    if (request.body.size() > 1024) return error(413, "BODY_TOO_LARGE", "Body too large");
    CommandJson command(request.body);
    if (!command.parse(request.path) || (request.path == "/api/v1/config" && !validConfig(command.config)))
        return error(400, "INVALID_REQUEST", "Invalid command");
    // Avoid mutating state if it cannot produce a trustworthy response.
    const auto before = controller.snapshot();
    if (before.revision >= 9007199254740991ULL ||
        handleStateRequest("GET", "/api/v1/state", before).status != 200)
        return error(503, "STATE_UNAVAILABLE", "State unavailable");
    if (request.path == "/api/v1/config") {
        const auto result = controller.setConfig(store, command.config);
        if (result == SaveResult::StorageFailure || result == SaveResult::NotLoaded)
            return error(503, "STORAGE_FAILURE", "Configuration not saved");
        if (result == SaveResult::Invalid) return error(400, "INVALID_REQUEST", "Invalid configuration");
    } else {
        const auto result = request.path == "/api/v1/power"
            ? controller.setPower(command.on, CommandSource::Web)
            : controller.setMode(command.mode == "auto" ? Mode::Auto : Mode::Manual, CommandSource::Web);
        if (result == CommandResult::TemperatureUnavailable)
            return error(409, "TEMPERATURE_UNAVAILABLE", "Temperature unavailable");
        if (result != CommandResult::Accepted) return error(400, "INVALID_REQUEST", "Invalid command");
    }
    return handleStateRequest("GET", "/api/v1/state", controller.snapshot());
}
} // namespace esp32fan
