#include "vesc/connection.hpp"

namespace vesc {

void Connection::receive(const uint8_t* data, size_t len) {
    decoder_.feed(data, len);
    while (decoder_.has_packet()) {
        auto payload = decoder_.pop();
        if (payload_cb_ && !payload.empty()) {
            payload_cb_(payload.data(), payload.size());
        }
    }
}

bool Connection::send(const std::vector<uint8_t>& payload) {
    if (!write_cb_) return false;

    auto pkt = encode_packet(payload.data(), payload.size());
    if (pkt.empty()) return false;

    size_t offset = 0;
    while (offset < pkt.size()) {
        size_t chunk = std::min(mtu_, pkt.size() - offset);
        write_cb_(pkt.data() + offset, chunk);
        offset += chunk;
    }
    return true;
}

void Connection::reset() {
    decoder_.reset();
}

} // namespace vesc
