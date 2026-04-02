#pragma once

// VESC connection codec.
//
// Handles the full wire protocol:
//   Raw bytes in → packet decode → payload callback
//   Payload out → packet encode → chunk to MTU → write callback
//
// Platform provides two things:
//   1. Write callback (send bytes to BLE/TCP)
//   2. Calls receive() when bytes arrive from BLE/TCP
//
// This is pure protocol logic — no BLE/TCP/platform code.

#include "vesc/protocol.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace vesc {

class Connection {
public:
    using WriteCallback = std::function<void(const uint8_t* data, size_t len)>;
    using PayloadCallback = std::function<void(const uint8_t* payload, size_t len)>;

    explicit Connection(size_t mtu = 512) : mtu_(mtu) {}

    void set_write_callback(WriteCallback cb) { write_cb_ = std::move(cb); }
    void set_payload_callback(PayloadCallback cb) { payload_cb_ = std::move(cb); }
    void set_mtu(size_t mtu) { mtu_ = mtu; }

    // Feed raw bytes from BLE/TCP. Decoded payloads fire the payload callback.
    void receive(const uint8_t* data, size_t len);

    // Send a VESC payload. Encodes as framed packet, chunks to MTU, writes.
    bool send(const std::vector<uint8_t>& payload);

    // Reset decoder state (call on disconnect).
    void reset();

private:
    PacketDecoder decoder_;
    WriteCallback write_cb_;
    PayloadCallback payload_cb_;
    size_t mtu_ = 512;
};

} // namespace vesc
