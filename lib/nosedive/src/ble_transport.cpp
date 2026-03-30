#include "nosedive/ble_transport.hpp"
#include "nosedive/commands.hpp"

namespace nosedive {

void BLETransport::on_ble_receive(const uint8_t* data, size_t len) {
    decoder_.feed(data, len);

    while (decoder_.has_packet()) {
        auto payload = decoder_.pop();
        if (packet_cb_ && !payload.empty()) {
            packet_cb_(payload.data(), payload.size());
        }
    }
}

bool BLETransport::send_payload(const uint8_t* payload, size_t len) {
    if (!send_cb_) return false;

    auto pkt = encode_packet(payload, len);
    if (pkt.empty()) return false;

    // Chunk to MTU
    size_t offset = 0;
    while (offset < pkt.size()) {
        size_t chunk = std::min(mtu_, pkt.size() - offset);
        send_cb_(pkt.data() + offset, chunk);
        offset += chunk;
    }
    return true;
}

bool BLETransport::send_command(uint8_t cmd) {
    return send_payload(&cmd, 1);
}

bool BLETransport::send_custom_app_data(const uint8_t* data, size_t len) {
    std::vector<uint8_t> payload;
    payload.reserve(1 + len);
    payload.push_back(static_cast<uint8_t>(CommPacketID::CustomAppData));
    payload.insert(payload.end(), data, data + len);
    return send_payload(payload.data(), payload.size());
}

void BLETransport::reset() {
    decoder_.reset();
}

} // namespace nosedive
