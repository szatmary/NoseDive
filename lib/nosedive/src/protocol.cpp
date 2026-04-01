#include "nosedive/protocol.hpp"
#include "nosedive/crc.hpp"

namespace nosedive {

static constexpr uint8_t kStartByteShort = 0x02;
static constexpr uint8_t kStartByteLong  = 0x03;
static constexpr uint8_t kEndByte        = 0x03;

std::vector<uint8_t> encode_packet(const uint8_t* payload, size_t len) {
    if (len == 0 || len > kMaxPayloadSize) return {};

    uint16_t crc = crc16(payload, len);
    std::vector<uint8_t> pkt;

    if (len < 256) {
        pkt.reserve(1 + 1 + len + 2 + 1);
        pkt.push_back(kStartByteShort);
        pkt.push_back(static_cast<uint8_t>(len));
    } else {
        pkt.reserve(1 + 2 + len + 2 + 1);
        pkt.push_back(kStartByteLong);
        pkt.push_back(static_cast<uint8_t>(len >> 8));
        pkt.push_back(static_cast<uint8_t>(len & 0xFF));
    }

    pkt.insert(pkt.end(), payload, payload + len);
    pkt.push_back(static_cast<uint8_t>(crc >> 8));
    pkt.push_back(static_cast<uint8_t>(crc & 0xFF));
    pkt.push_back(kEndByte);

    return pkt;
}

std::optional<DecodeResult> decode_packet(const uint8_t* data, size_t len) {
    // Scan for start byte
    size_t pos = 0;
    while (pos < len && data[pos] != kStartByteShort && data[pos] != kStartByteLong) {
        pos++;
    }
    if (pos >= len) return std::nullopt;

    uint8_t start_type = data[pos];
    pos++;

    size_t payload_len;
    if (start_type == kStartByteShort) {
        if (pos >= len) return std::nullopt;
        payload_len = data[pos];
        pos++;
    } else {
        if (pos + 1 >= len) return std::nullopt;
        payload_len = (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
        pos += 2;
    }

    if (payload_len == 0 || payload_len > kMaxPayloadSize) return std::nullopt;

    // Need payload + 2 byte CRC + 1 byte end
    if (pos + payload_len + 3 > len) return std::nullopt;

    const uint8_t* payload_ptr = data + pos;
    uint16_t crc_received = (static_cast<uint16_t>(data[pos + payload_len]) << 8) |
                             data[pos + payload_len + 1];
    uint8_t end = data[pos + payload_len + 2];

    if (end != kEndByte) return std::nullopt;

    uint16_t crc_calc = crc16(payload_ptr, payload_len);
    if (crc_calc != crc_received) return std::nullopt;

    DecodeResult result;
    result.payload.assign(payload_ptr, payload_ptr + payload_len);
    result.bytes_consumed = pos + payload_len + 3;
    return result;
}

// --- Buffer implementation ---

void Buffer::append_uint8(uint8_t v) {
    data_.push_back(v);
}

void Buffer::append_int16(int16_t v) {
    data_.push_back(static_cast<uint8_t>(v >> 8));
    data_.push_back(static_cast<uint8_t>(v));
}

void Buffer::append_uint16(uint16_t v) {
    data_.push_back(static_cast<uint8_t>(v >> 8));
    data_.push_back(static_cast<uint8_t>(v));
}

void Buffer::append_int32(int32_t v) {
    uint32_t u = static_cast<uint32_t>(v);
    data_.push_back(static_cast<uint8_t>(u >> 24));
    data_.push_back(static_cast<uint8_t>(u >> 16));
    data_.push_back(static_cast<uint8_t>(u >> 8));
    data_.push_back(static_cast<uint8_t>(u));
}

void Buffer::append_uint32(uint32_t v) {
    data_.push_back(static_cast<uint8_t>(v >> 24));
    data_.push_back(static_cast<uint8_t>(v >> 16));
    data_.push_back(static_cast<uint8_t>(v >> 8));
    data_.push_back(static_cast<uint8_t>(v));
}

void Buffer::append_float16(double v, double scale) {
    append_int16(static_cast<int16_t>(v * scale));
}

void Buffer::append_float32(double v, double scale) {
    append_int32(static_cast<int32_t>(v * scale));
}

void Buffer::append_float32_auto(double v) {
    float f = static_cast<float>(v);
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    append_uint32(bits);
}

void Buffer::append_bytes(const uint8_t* data, size_t len) {
    data_.insert(data_.end(), data, data + len);
}

void Buffer::append_string(const std::string& s) {
    data_.insert(data_.end(), s.begin(), s.end());
    data_.push_back(0); // null terminator
}

uint8_t Buffer::read_uint8() {
    if (read_pos_ >= data_.size()) { read_pos_ = data_.size(); return 0; }
    return data_[read_pos_++];
}

int16_t Buffer::read_int16() {
    if (read_pos_ + 2 > data_.size()) { read_pos_ = data_.size(); return 0; }
    int16_t v = static_cast<int16_t>(
        (static_cast<uint16_t>(data_[read_pos_]) << 8) | data_[read_pos_ + 1]);
    read_pos_ += 2;
    return v;
}

uint16_t Buffer::read_uint16() {
    if (read_pos_ + 2 > data_.size()) { read_pos_ = data_.size(); return 0; }
    uint16_t v = (static_cast<uint16_t>(data_[read_pos_]) << 8) | data_[read_pos_ + 1];
    read_pos_ += 2;
    return v;
}

int32_t Buffer::read_int32() {
    if (read_pos_ + 4 > data_.size()) { read_pos_ = data_.size(); return 0; }
    uint32_t u = (static_cast<uint32_t>(data_[read_pos_]) << 24) |
                 (static_cast<uint32_t>(data_[read_pos_ + 1]) << 16) |
                 (static_cast<uint32_t>(data_[read_pos_ + 2]) << 8) |
                  data_[read_pos_ + 3];
    read_pos_ += 4;
    return static_cast<int32_t>(u);
}

uint32_t Buffer::read_uint32() {
    if (read_pos_ + 4 > data_.size()) { read_pos_ = data_.size(); return 0; }
    uint32_t v = (static_cast<uint32_t>(data_[read_pos_]) << 24) |
                 (static_cast<uint32_t>(data_[read_pos_ + 1]) << 16) |
                 (static_cast<uint32_t>(data_[read_pos_ + 2]) << 8) |
                  data_[read_pos_ + 3];
    read_pos_ += 4;
    return v;
}

double Buffer::read_float16(double scale) {
    return static_cast<double>(read_int16()) / scale;
}

double Buffer::read_float32(double scale) {
    return static_cast<double>(read_int32()) / scale;
}

double Buffer::read_float32_auto() {
    uint32_t bits = read_uint32();
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return static_cast<double>(f);
}

std::string Buffer::read_string() {
    size_t start = read_pos_;
    while (read_pos_ < data_.size() && data_[read_pos_] != 0) {
        read_pos_++;
    }
    std::string s(data_.begin() + start, data_.begin() + read_pos_);
    if (read_pos_ < data_.size()) read_pos_++; // skip null
    return s;
}

// --- PacketDecoder implementation ---

void PacketDecoder::feed(const uint8_t* data, size_t len) {
    buf_.insert(buf_.end(), data, data + len);

    // Try to extract complete packets from the buffer
    while (!buf_.empty()) {
        auto result = decode_packet(buf_.data(), buf_.size());
        if (!result) break;

        packets_.push_back(std::move(result->payload));
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<ptrdiff_t>(result->bytes_consumed));
    }
}

std::vector<uint8_t> PacketDecoder::pop() {
    if (packets_.empty()) return {};
    auto pkt = std::move(packets_.front());
    packets_.erase(packets_.begin());
    return pkt;
}

void PacketDecoder::reset() {
    buf_.clear();
    packets_.clear();
}

} // namespace nosedive
