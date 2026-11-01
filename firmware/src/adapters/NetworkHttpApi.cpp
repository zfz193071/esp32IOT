#include "NetworkHttpApi.h"

namespace esp32fan {
NetworkHttpApi::NetworkHttpApi(HttpListener& listener, std::uint16_t port)
    : listener_(listener), http_(listener), port_(port) {}

NetworkHttpApi::~NetworkHttpApi() {
    http_.stop(); // discard the request/response before retiring the listener
    listener_.end();
}

void NetworkHttpApi::update(std::uint32_t nowMs, const NetworkUpdate& network) {
    // Only retain bounded IPv4-sized strings from the trusted network port.
    // The socket driver validates numeric syntax before binding; never use Host.
    const bool online = network.connected && !network.address.empty() && network.address.size() <= 15;
    const std::string target = online ? network.address : std::string();
    if (target != address_) {
        http_.stop();
        listener_.end();
        listening_ = false;
        address_ = target;
    }
    if (!online) return;
    if (!listening_) {
        // Keep this budget across link/address changes so flapping cannot bypass
        // retry pacing. The first attempt is immediate; subtraction handles wrap.
        if (attempted_ && nowMs - attemptedAtMs_ < retryDelayMs) return;
        attempted_ = true;
        attemptedAtMs_ = nowMs;
        listening_ = listener_.begin(address_.c_str(), port_);
        if (!listening_) listener_.end();
        return; // listener creation and accept occur in different loop cycles
    }
    http_.poll(nowMs);
}

bool NetworkHttpApi::takeRequest(WriteRequest& request) {
    return listening_ && http_.takeRequest(request);
}

void NetworkHttpApi::sendResponse(const StateResponse& response) {
    if (listening_) http_.sendResponse(response);
}

bool NetworkHttpApi::listening() const { return listening_; }
} // namespace esp32fan
