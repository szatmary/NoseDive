#pragma once

// BLE transport abstraction layer.
//
// The C++ library needs to send/receive VESC packets over BLE, but BLE stacks
// are platform-specific (CoreBluetooth on iOS, Android BLE on Android).
//
// This is solved with a callback-based design:
// 1. Platform code (Swift/Kotlin) implements the send callback
// 2. Platform code feeds received BLE data into the transport
// 3. The transport handles VESC packet framing/reassembly internally
// 4. Parsed packets are delivered via a receive callback
//
// Flow:
//   iOS/Android BLE notification → nd_transport_receive() → PacketDecoder → on_packet callback
//   C++ wants to send           → encode_packet → chunk to MTU → send callback → iOS/Android BLE write

#include "nosedive/protocol.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace nosedive {

class BLETransport {
public:
    // Callback types
    using SendCallback = std::function<void(const uint8_t* data, size_t len)>;
    using PacketCallback = std::function<void(const uint8_t* payload, size_t len)>;

    explicit BLETransport(size_t mtu = 20) : mtu_(mtu) {}

    // Set the callback that writes raw bytes to the BLE characteristic.
    // The platform layer implements this (CoreBluetooth / Android BLE).
    void set_send_callback(SendCallback cb) { send_cb_ = std::move(cb); }

    // Set the callback invoked when a complete VESC packet payload is received.
    void set_packet_callback(PacketCallback cb) { packet_cb_ = std::move(cb); }

    // Set BLE MTU (default 20 for NUS). Call after MTU negotiation.
    void set_mtu(size_t mtu) { mtu_ = mtu; }

    // Called by platform code when a BLE notification arrives (raw bytes).
    // Internally reassembles VESC packets and fires the packet callback.
    void on_ble_receive(const uint8_t* data, size_t len);

    // Send a VESC payload. Encodes it as a packet, chunks to MTU, and
    // calls the send callback for each chunk.
    bool send_payload(const uint8_t* payload, size_t len);

    // Send a pre-built VESC command (single command byte).
    bool send_command(uint8_t cmd);

    // Send a COMM_CUSTOM_APP_DATA wrapping the given data.
    bool send_custom_app_data(const uint8_t* data, size_t len);

    // Reset the internal packet decoder state.
    void reset();

private:
    PacketDecoder decoder_;
    SendCallback send_cb_;
    PacketCallback packet_cb_;
    size_t mtu_ = 20;
};

} // namespace nosedive
