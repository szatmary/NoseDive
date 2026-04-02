#include <catch2/catch_test_macros.hpp>
#include "vesc/vesc.hpp"

TEST_CASE("CRC16 is non-zero and deterministic", "[crc]") {
    uint8_t data[] = {0x04};
    uint16_t crc = vesc::crc16(data, 1);
    REQUIRE(crc != 0);
    REQUIRE(vesc::crc16(data, 1) == crc);
}

TEST_CASE("Packet encode/decode roundtrip", "[packet]") {
    uint8_t payload[] = {0x04};
    auto pkt = vesc::encode_packet(payload, 1);
    REQUIRE_FALSE(pkt.empty());

    auto decoded = vesc::decode_packet(pkt.data(), pkt.size());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->payload.size() == 1);
    REQUIRE(decoded->payload[0] == 0x04);
}

TEST_CASE("PacketDecoder streaming", "[packet]") {
    uint8_t payload[] = {0x04};
    auto pkt = vesc::encode_packet(payload, 1);

    vesc::PacketDecoder decoder;
    decoder.feed(pkt.data(), pkt.size());
    REQUIRE(decoder.has_packet());
    auto popped = decoder.pop();
    REQUIRE(popped.size() == 1);
    REQUIRE(popped[0] == 0x04);
}
