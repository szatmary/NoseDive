#include "vesc/vesc.hpp"
#include <cstdio>

int main() {
    // Verify CRC16
    uint8_t data[] = {0x04};
    uint16_t crc = vesc::crc16(data, 1);
    if (crc == 0) { printf("FAIL: crc is zero\n"); return 1; }

    // Verify encode/decode roundtrip
    auto pkt = vesc::encode_packet(data, 1);
    if (pkt.empty()) { printf("FAIL: encode empty\n"); return 1; }

    auto decoded = vesc::decode_packet(pkt.data(), pkt.size());
    if (!decoded) { printf("FAIL: decode failed\n"); return 1; }
    if (decoded->payload.size() != 1 || decoded->payload[0] != 0x04) {
        printf("FAIL: roundtrip mismatch\n"); return 1;
    }

    // Verify PacketDecoder
    vesc::PacketDecoder decoder;
    decoder.feed(pkt.data(), pkt.size());
    if (!decoder.has_packet()) { printf("FAIL: decoder no packet\n"); return 1; }
    auto popped = decoder.pop();
    if (popped.size() != 1 || popped[0] != 0x04) { printf("FAIL: decoder mismatch\n"); return 1; }

    printf("All libvesc tests passed\n");
    return 0;
}
