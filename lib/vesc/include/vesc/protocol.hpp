#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <optional>
#include <string>
#include <cstring>
#include <cmath>

namespace vesc {

constexpr size_t kMaxPayloadSize = 8192;

// --- Packet framing ---

// Encode a VESC payload into wire-format packet.
// Short (<256 bytes): [0x02][len:1][payload][crc16:2][0x03]
// Long (>=256 bytes): [0x03][len:2][payload][crc16:2][0x03]
std::vector<uint8_t> encode_packet(const uint8_t* payload, size_t len);

// Decode result
struct DecodeResult {
    std::vector<uint8_t> payload;
    size_t bytes_consumed; // how many bytes from the input were consumed
};

// Try to decode one VESC packet from a buffer. Returns nullopt if incomplete.
std::optional<DecodeResult> decode_packet(const uint8_t* data, size_t len);

// --- Push-based packet decoder for BLE chunk reassembly ---
// BLE delivers data in ~20-byte chunks. This decoder accumulates bytes and
// emits complete packets as they arrive. Thread-safe if externally synchronized.

class PacketDecoder {
public:
    PacketDecoder() = default;

    // Feed raw bytes from BLE notifications. After feeding, call pop() to
    // retrieve complete packets.
    void feed(const uint8_t* data, size_t len);

    // Returns true if at least one complete packet is available.
    bool has_packet() const { return !packets_.empty(); }

    // Number of complete packets ready to pop.
    size_t packet_count() const { return packets_.size(); }

    // Pop the next complete payload (FIFO). Returns empty if none available.
    std::vector<uint8_t> pop();

    // Discard all buffered data and queued packets.
    void reset();

private:
    std::vector<uint8_t> buf_;     // accumulation buffer
    std::vector<std::vector<uint8_t>> packets_; // complete payloads
};

// --- Big-endian buffer helpers (matching VESC buffer.h) ---

class Buffer {
public:
    Buffer() = default;
    explicit Buffer(std::vector<uint8_t> data) : data_(std::move(data)), read_pos_(0) {}

    // Write operations (append)
    void append_uint8(uint8_t v);
    void append_int16(int16_t v);
    void append_uint16(uint16_t v);
    void append_int32(int32_t v);
    void append_uint32(uint32_t v);
    void append_float16(double v, double scale);
    void append_float32(double v, double scale);
    void append_float32_auto(double v);
    void append_bytes(const uint8_t* data, size_t len);
    void append_string(const std::string& s);

    // Read operations (consume from read position)
    uint8_t read_uint8();
    int16_t read_int16();
    uint16_t read_uint16();
    int32_t read_int32();
    uint32_t read_uint32();
    double read_float16(double scale);
    double read_float32(double scale);
    double read_float32_auto();
    std::string read_string();

    // Access
    const uint8_t* data() const { return data_.data(); }
    size_t size() const { return data_.size(); }
    size_t read_pos() const { return read_pos_; }

    // Move the internal buffer out (for building payloads)
    std::vector<uint8_t> take() { return std::move(data_); }
    size_t remaining() const { return data_.size() - read_pos_; }
    void reset_read() { read_pos_ = 0; }

    std::vector<uint8_t>& vec() { return data_; }
    const std::vector<uint8_t>& vec() const { return data_; }

private:
    std::vector<uint8_t> data_;
    size_t read_pos_ = 0;
};

} // namespace vesc
