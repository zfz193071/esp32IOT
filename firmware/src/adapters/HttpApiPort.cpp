#include "HttpApiPort.h"
#include "WebAssets.h"

#include <algorithm>
#include <utility>

namespace esp32fan {
namespace {
const char* reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 413: return "Content Too Large";
    case 414: return "URI Too Long";
    case 417: return "Expectation Failed";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    case 505: return "HTTP Version Not Supported";
    default: return nullptr;
    }
}
bool safeHeader(const char* value, std::size_t limit) {
    if (!value || !*value) return false;
    for (std::size_t i = 0; i <= limit; ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (c == 0) return true;
        if (i == limit || c < 0x20 || c > 0x7e) return false;
    }
    return false;
}
} // namespace

HttpApiPort::HttpApiPort(HttpTransport& transport) : transport_(transport) {}
HttpApiPort::~HttpApiPort() { stop(); }

void HttpApiPort::stop() {
    if (phase_ != Phase::Idle) transport_.close();
    phase_ = Phase::Idle;
    parser_ = HttpRequestParser(0);
    request_ = WriteRequest{};
    headers_.clear();
    body_.clear();
    assetData_ = nullptr;
    bodySize_ = headerOffset_ = bodyOffset_ = 0;
    head_ = false;
}

void HttpApiPort::poll(std::uint32_t nowMs) {
    if (phase_ == Phase::Idle) {
        if (transport_.accept()) {
            parser_ = HttpRequestParser(nowMs);
            phase_ = Phase::Receiving;
        }
        return; // accepting and reading are separate bounded steps
    }
    if (phase_ != Phase::Receiving) {
        if (nowMs - responseAtMs_ >= responseTimeoutMs) { stop(); return; }
        if (phase_ == Phase::Sending) sendChunk();
        return;
    }
    parser_.tick(nowMs);
    if (parser_.status() == HttpParseStatus::Receiving) {
        char bytes[maxIoBytes];
        const auto result = transport_.receive(bytes, sizeof(bytes));
        switch (result.status) {
        case HttpIoStatus::Progress:
            if (result.size == 0 || result.size > sizeof(bytes)) { stop(); return; }
            // A pipelined tail is deliberately discarded, never parsed again.
            parser_.feed(bytes, result.size, nowMs);
            break;
        case HttpIoStatus::WouldBlock: break;
        case HttpIoStatus::End: parser_.endOfInput(); break;
        case HttpIoStatus::Error: stop(); return;
        }
    }
    route(nowMs);
}

void HttpApiPort::route(std::uint32_t nowMs) {
    if (parser_.status() == HttpParseStatus::Receiving) return;
    responseAtMs_ = nowMs;
    if (parser_.status() == HttpParseStatus::Rejected) {
        queueError(parser_.errorStatus());
        return;
    }
    parser_.takeRequest(request_);
    head_ = request_.method == "HEAD";
    if (request_.path.compare(0, 5, "/api/") == 0) {
        phase_ = Phase::Ready;
        return;
    }
    const auto* asset = web::findAsset(request_.path.c_str());
    if (!asset) queueError(404);
    else if (request_.method != "GET" && !head_) queueError(405, "GET, HEAD");
    else {
        assetData_ = asset->data;
        queueHeaders(200, asset->contentType, "no-store", nullptr, asset->size);
    }
    request_ = WriteRequest{};
}

bool HttpApiPort::takeRequest(WriteRequest& request) {
    if (phase_ != Phase::Ready) return false;
    request = std::move(request_);
    phase_ = Phase::AwaitResponse;
    return true;
}

void HttpApiPort::sendResponse(const StateResponse& response) {
    if (phase_ != Phase::AwaitResponse) return;
    if (!reason(response.status) || response.body.size() > maxResponseBodyBytes ||
        !safeHeader(response.contentType, 128) || !safeHeader(response.cacheControl, 64) ||
        (response.allow && !safeHeader(response.allow, 32))) {
        queueError(500);
        return;
    }
    body_ = response.body; // own the bounded API body after AppLoop returns
    queueHeaders(response.status, response.contentType, response.cacheControl,
                 response.allow, body_.size());
}

void HttpApiPort::queueError(int status, const char* allow) {
    body_ = "{\"error\":{\"code\":\"HTTP_ERROR\",\"message\":\"";
    body_ += reason(status);
    body_ += "\"}}";
    queueHeaders(status, "application/json; charset=utf-8", "no-store", allow, body_.size());
}

void HttpApiPort::queueHeaders(int status, const char* type, const char* cache,
                              const char* allow, std::size_t length) {
    headers_ = "HTTP/1.1 " + std::to_string(status) + " " + reason(status) + "\r\n";
    headers_ += std::string("Content-Type: ") + type + "\r\n";
    headers_ += "Content-Length: " + std::to_string(length) + "\r\n";
    headers_ += std::string("Cache-Control: ") + cache + "\r\n";
    headers_ += "Connection: close\r\n";
    if (allow) headers_ += std::string("Allow: ") + allow + "\r\n";
    headers_ += "\r\n";
    bodySize_ = head_ ? 0 : length;
    headerOffset_ = bodyOffset_ = 0;
    phase_ = Phase::Sending;
}

void HttpApiPort::sendChunk() {
    const bool header = headerOffset_ < headers_.size();
    auto& offset = header ? headerOffset_ : bodyOffset_;
    const auto remaining = header ? headers_.size() - offset : bodySize_ - offset;
    if (remaining == 0) { stop(); return; }
    const char* data = header ? headers_.data() :
        (assetData_ ? reinterpret_cast<const char*>(assetData_) : body_.data());
    const auto size = std::min(remaining, static_cast<std::size_t>(maxIoBytes));
    const auto result = transport_.send(data + offset, size);
    if (result.status == HttpIoStatus::WouldBlock) return;
    if (result.status != HttpIoStatus::Progress || result.size == 0 || result.size > size) {
        stop();
        return;
    }
    offset += result.size;
    if (headerOffset_ == headers_.size() && bodyOffset_ == bodySize_) stop();
}

} // namespace esp32fan
