#pragma once

// VESC connection codec.
//
// Handles the full wire protocol:
//   Raw bytes in → packet decode → payload callback
//   Payload out → packet encode → write callback
//
// Platform provides two things:
//   1. Write callback (send framed bytes to BLE/TCP)
//   2. Calls receive() when bytes arrive from BLE/TCP

#include "vesc/protocol.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace vesc {

class Connection {
public:
    using WriteCallback = std::function<void(const uint8_t* data, size_t len)>;
    using PayloadCallback = std::function<void(const uint8_t* payload, size_t len)>;

    void set_write_callback(WriteCallback cb) { write_cb_ = std::move(cb); }
    void set_payload_callback(PayloadCallback cb) { payload_cb_ = std::move(cb); }

    // Feed raw bytes from BLE/TCP. Decoded payloads fire the payload callback.
    void receive(const uint8_t* data, size_t len);

    // Send a VESC payload. Encodes as framed packet, writes via callback.
    bool send(const std::vector<uint8_t>& payload);

    // Reset decoder state (call on disconnect).
    void reset();

private:
    PacketDecoder decoder_;
    WriteCallback write_cb_;
    PayloadCallback payload_cb_;
};

} // namespace vesc
