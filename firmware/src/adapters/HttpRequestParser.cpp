#include "HttpRequestParser.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace esp32fan {
namespace {
bool tokenChar(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           (c != 0 && std::strchr("!#$%&'*+-.^_`|~", c) != nullptr);
}
std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    return value.substr(first, value.find_last_not_of(" \t") - first + 1);
}
bool validHost(const std::string& host) {
    // This direct-LAN adapter supports DNS/IPv4 authorities with optional port.
    // IPv6/proxy authority handling requires a separate transport extension.
    const auto colon = host.find(':');
    const auto name = host.substr(0, colon);
    if (name.empty()) return false;
    for (const unsigned char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
    }
    if (colon == std::string::npos) return true;
    const auto port = host.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    unsigned value = 0;
    for (const unsigned char c : port) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + c - '0';
    }
    return value > 0 && value <= 65535;
}
} // namespace

HttpRequestParser::HttpRequestParser(std::uint32_t acceptedAtMs) : acceptedAtMs_(acceptedAtMs) {}

void HttpRequestParser::reject(int status) {
    status_ = HttpParseStatus::Rejected;
    errorStatus_ = status;
    line_.clear();
    request_ = WriteRequest{};
}

void HttpRequestParser::tick(std::uint32_t nowMs) {
    if (status_ == HttpParseStatus::Receiving && nowMs - acceptedAtMs_ >= receiveTimeoutMs)
        reject(408);
}

void HttpRequestParser::endOfInput() {
    if (status_ == HttpParseStatus::Receiving) reject(400);
}

HttpParseStatus HttpRequestParser::status() const { return status_; }
int HttpRequestParser::errorStatus() const { return errorStatus_; }

bool HttpRequestParser::takeRequest(WriteRequest& output) {
    if (status_ != HttpParseStatus::Complete || taken_) return false;
    output = std::move(request_);
    taken_ = true;
    return true;
}

std::size_t HttpRequestParser::feed(const char* bytes, std::size_t size, std::uint32_t nowMs) {
    tick(nowMs);
    if (status_ != HttpParseStatus::Receiving) return 0;
    if (!bytes && size != 0) { reject(400); return 0; }
    const auto budget = std::min(size, static_cast<std::size_t>(maxBytesPerFeed));
    std::size_t consumed = 0;
    while (consumed < budget && status_ == HttpParseStatus::Receiving) {
        receiveByte(static_cast<unsigned char>(bytes[consumed]));
        ++consumed;
    }
    return consumed;
}

void HttpRequestParser::receiveByte(unsigned char byte) {
    if (phase_ == Phase::Body) {
        request_.body += static_cast<char>(byte);
        if (request_.body.size() == bodyLength_) status_ = HttpParseStatus::Complete;
        return;
    }
    if (++headerBytes_ > maxHeaderBytes) { reject(431); return; }
    if (sawCr_) {
        if (byte != '\n') { reject(400); return; }
        sawCr_ = false;
        if (phase_ == Phase::RequestLine) parseRequestLine();
        else if (line_.empty()) finishHeaders();
        else parseHeader();
        line_.clear();
        return;
    }
    if (byte == '\r') { sawCr_ = true; return; }
    if (byte == '\n' || byte == 0x7f || byte >= 0x80 || (byte < 0x20 && byte != '\t')) {
        reject(400); return;
    }
    if (line_.size() >= maxLineBytes) {
        reject(phase_ == Phase::RequestLine ? 414 : 431); return;
    }
    line_ += static_cast<char>(byte);
}

void HttpRequestParser::parseRequestLine() {
    const auto first = line_.find(' ');
    const auto second = first == std::string::npos ? first : line_.find(' ', first + 1);
    if (first == std::string::npos || first == 0 || first > 16 ||
        second == std::string::npos || second == first + 1 ||
        line_.find(' ', second + 1) != std::string::npos) { reject(400); return; }
    for (std::size_t i = 0; i < first; ++i) {
        if (!tokenChar(static_cast<unsigned char>(line_[i]))) { reject(400); return; }
    }
    if (line_.substr(second + 1) != "HTTP/1.1") { reject(505); return; }
    const auto target = line_.substr(first + 1, second - first - 1);
    if (target[0] != '/' || target.find_first_of("#\\\t") != std::string::npos) {
        reject(400); return;
    }
    const auto path = target.substr(0, target.find('?'));
    if (path.size() > 128) { reject(414); return; }
    request_.method = line_.substr(0, first);
    // No percent decoding or normalization: route matching uses the exact path.
    request_.path = path;
    phase_ = Phase::Headers;
}

void HttpRequestParser::parseHeader() {
    const auto colon = line_.find(':');
    if (colon == std::string::npos || colon == 0) { reject(400); return; }
    auto name = line_.substr(0, colon);
    for (char& c : name) {
        if (!tokenChar(static_cast<unsigned char>(c))) { reject(400); return; }
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    const auto value = trim(line_.substr(colon + 1));
    if (name == "host") {
        if (hasHost_ || value.empty()) { reject(400); return; }
        if (value.size() > 128) { reject(431); return; }
        if (!validHost(value)) { reject(400); return; }
        request_.host = value;
        hasHost_ = true;
    } else if (name == "origin") {
        if (request_.hasOrigin) { reject(400); return; }
        if (value.size() > 256) { reject(431); return; }
        request_.hasOrigin = true;
        request_.origin = value;
    } else if (name == "content-type") {
        if (hasContentType_) { reject(400); return; }
        if (value.size() > 128) { reject(431); return; }
        request_.contentType = value;
        hasContentType_ = true;
    } else if (name == "content-length") {
        if (hasLength_ || value.empty()) { reject(400); return; }
        // Validate all digits before interpreting the bounded length.
        for (const unsigned char c : value) {
            if (c < '0' || c > '9') { reject(400); return; }
        }
        for (const unsigned char c : value) {
            const auto digit = static_cast<std::size_t>(c - '0');
            if (bodyLength_ > (maxBodyBytes - digit) / 10) { reject(413); return; }
            bodyLength_ = bodyLength_ * 10 + digit;
        }
        hasLength_ = true;
    } else if (name == "transfer-encoding") {
        // This bounded subset does not accept chunked or mixed framing.
        reject(400);
    } else if (name == "expect") {
        reject(417);
    }
}

void HttpRequestParser::finishHeaders() {
    if (!hasHost_) { reject(400); return; }
    if (request_.method == "PUT" && !hasLength_) { reject(411); return; }
    if (bodyLength_ == 0) status_ = HttpParseStatus::Complete;
    else phase_ = Phase::Body;
}

} // namespace esp32fan
