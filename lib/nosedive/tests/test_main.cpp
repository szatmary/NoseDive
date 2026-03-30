// Minimal test runner — no dependencies
#include "nosedive/nosedive.hpp"
#include "nosedive/ffi.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s (line %d): %s\n", msg, __LINE__, #cond); \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)
#define ASSERT_NEAR(a, b, eps, msg) ASSERT(std::fabs((a) - (b)) < (eps), msg)

// --- CRC tests ---
static void test_crc16() {
    // Known VESC CRC: empty-ish payload
    uint8_t data[] = {0x04}; // COMM_GET_VALUES
    uint16_t crc = nosedive::crc16(data, 1);
    // Just verify it returns something non-zero and deterministic
    ASSERT(crc != 0, "crc16 non-zero for non-empty input");
    ASSERT_EQ(nosedive::crc16(data, 1), crc, "crc16 deterministic");

    // Verify C FFI matches
    ASSERT_EQ(nd_crc16(data, 1), crc, "nd_crc16 matches C++ crc16");
}

// --- Packet encode/decode round-trip ---
static void test_packet_roundtrip_short() {
    uint8_t payload[] = {0x04};
    auto pkt = nosedive::encode_packet(payload, 1);
    ASSERT(!pkt.empty(), "encode_packet short non-empty");
    ASSERT_EQ(pkt[0], 0x02, "short packet starts with 0x02");
    ASSERT_EQ(pkt[1], 1, "short packet length byte");
    ASSERT_EQ(pkt.back(), 0x03, "packet ends with 0x03");

    auto decoded = nosedive::decode_packet(pkt.data(), pkt.size());
    ASSERT(decoded.has_value(), "decode_packet succeeds");
    ASSERT_EQ(decoded->payload.size(), 1u, "decoded payload size");
    ASSERT_EQ(decoded->payload[0], 0x04, "decoded payload content");
    ASSERT_EQ(decoded->bytes_consumed, pkt.size(), "consumed all bytes");
}

static void test_packet_roundtrip_long() {
    // Create a 300-byte payload (long packet)
    std::vector<uint8_t> payload(300, 0xAB);
    payload[0] = 0x14; // some command
    auto pkt = nosedive::encode_packet(payload.data(), payload.size());
    ASSERT(!pkt.empty(), "encode_packet long non-empty");
    ASSERT_EQ(pkt[0], 0x03, "long packet starts with 0x03");

    auto decoded = nosedive::decode_packet(pkt.data(), pkt.size());
    ASSERT(decoded.has_value(), "decode long packet succeeds");
    ASSERT_EQ(decoded->payload.size(), 300u, "decoded long payload size");
    ASSERT_EQ(decoded->payload[0], 0x14, "decoded long payload first byte");
}

// --- Buffer helpers ---
static void test_buffer_int16() {
    nosedive::Buffer buf;
    buf.append_int16(-1234);
    buf.append_int16(5678);

    nosedive::Buffer reader(buf.vec());
    ASSERT_EQ(reader.read_int16(), -1234, "int16 round-trip negative");
    ASSERT_EQ(reader.read_int16(), 5678, "int16 round-trip positive");
}

static void test_buffer_int32() {
    nosedive::Buffer buf;
    buf.append_int32(-123456);
    buf.append_uint32(0xDEADBEEF);

    nosedive::Buffer reader(buf.vec());
    ASSERT_EQ(reader.read_int32(), -123456, "int32 round-trip");
    ASSERT_EQ(reader.read_uint32(), 0xDEADBEEFu, "uint32 round-trip");
}

static void test_buffer_float16() {
    nosedive::Buffer buf;
    buf.append_float16(25.5, 10.0);

    nosedive::Buffer reader(buf.vec());
    ASSERT_NEAR(reader.read_float16(10.0), 25.5, 0.11, "float16 round-trip");
}

static void test_buffer_float32_auto() {
    nosedive::Buffer buf;
    double values[] = {0.0, 1.0, -1.0, 3.14159, 1e-7, 1e7};
    for (double v : values) {
        buf.append_float32_auto(v);
    }

    nosedive::Buffer reader(buf.vec());
    for (double v : values) {
        double got = reader.read_float32_auto();
        if (v == 0.0) {
            ASSERT_EQ(got, 0.0, "float32_auto zero");
        } else {
            double rel = std::fabs((got - v) / v);
            ASSERT(rel < 1e-6, "float32_auto round-trip precision");
        }
    }
}

static void test_buffer_string() {
    nosedive::Buffer buf;
    buf.append_string("hello");
    buf.append_string("world");

    nosedive::Buffer reader(buf.vec());
    ASSERT(reader.read_string() == "hello", "string round-trip 1");
    ASSERT(reader.read_string() == "world", "string round-trip 2");
}

// --- FFI packet round-trip ---
static void test_ffi_packet() {
    uint8_t payload[] = {0x04, 0x01, 0x02};
    size_t pkt_len = 0;
    uint8_t* pkt = nd_encode_packet(payload, 3, &pkt_len);
    ASSERT(pkt != nullptr, "nd_encode_packet non-null");
    ASSERT(pkt_len > 3, "nd_encode_packet adds framing");

    size_t out_len = 0, consumed = 0;
    uint8_t* decoded = nd_decode_packet(pkt, pkt_len, &out_len, &consumed);
    ASSERT(decoded != nullptr, "nd_decode_packet non-null");
    ASSERT_EQ(out_len, 3u, "ffi decoded payload size");
    ASSERT_EQ(decoded[0], 0x04, "ffi decoded content");
    ASSERT_EQ(consumed, pkt_len, "ffi consumed all");

    nd_free(decoded);
    nd_free(pkt);
}

// --- Profile loading ---
static void test_profile_load() {
    const char* json = R"({
        "name": "Test Board",
        "manufacturer": "TestCo",
        "model": "T1",
        "description": "A test board",
        "controller": {
            "type": "VESC 6",
            "hardware": "6.7",
            "firmware": { "major": 6, "minor": 5 },
            "maxCurrent": 80,
            "maxBrakeCurrent": -60
        },
        "motor": {
            "type": "hub",
            "name": "Test Motor",
            "polePairs": 15,
            "resistance": 0.088,
            "inductance": 0.000233,
            "fluxLinkage": 0.028,
            "maxCurrent": 60,
            "maxBrakeCurrent": -40,
            "kv": 60,
            "hallSensorTable": [255, 1, 3, 2, 5, 6, 4, 255]
        },
        "battery": {
            "chemistry": "Li-ion",
            "cellType": "Samsung 40T",
            "configuration": "20s2p",
            "seriesCells": 20,
            "parallelCells": 2,
            "capacityAh": 8.0,
            "capacityWh": 576,
            "voltageMin": 60.0,
            "voltageNominal": 72.0,
            "voltageMax": 84.0,
            "cutoffStart": 64.0,
            "cutoffEnd": 60.0,
            "maxDischargeCurrent": 70,
            "maxChargeCurrent": 10,
            "cellMinVoltage": 3.0,
            "cellMaxVoltage": 4.2,
            "cellNominalVoltage": 3.6
        },
        "wheel": {
            "diameter": 11.0,
            "diameterUnit": "inches",
            "tirePressurePSI": 20,
            "circumferenceM": 0.8778
        },
        "performance": {
            "topSpeedMPH": 25,
            "rangeMiles": 15,
            "weightLbs": 28
        }
    })";

    auto profile = nosedive::load_profile(json);
    ASSERT(profile.has_value(), "load_profile succeeds");
    ASSERT(profile->name == "Test Board", "profile name");
    ASSERT(profile->manufacturer == "TestCo", "profile manufacturer");
    ASSERT_EQ(profile->motor.pole_pairs, 15, "profile pole pairs");
    ASSERT_NEAR(profile->motor.resistance, 0.088, 1e-6, "profile resistance");
    ASSERT_NEAR(profile->motor.inductance, 0.000233, 1e-9, "profile inductance");
    ASSERT_NEAR(profile->motor.flux_linkage, 0.028, 1e-6, "profile flux");
    ASSERT_EQ(profile->battery.series_cells, 20, "profile series cells");
    ASSERT_NEAR(profile->battery.capacity_wh, 576.0, 0.1, "profile capacity");
    ASSERT_EQ(static_cast<int>(profile->motor.hall_sensor_table.size()), 8, "hall table size");
    ASSERT_EQ(profile->motor.hall_sensor_table[0], 255, "hall table entry 0");
    ASSERT_EQ(profile->motor.hall_sensor_table[1], 1, "hall table entry 1");

    // Computed values
    double erpm_per_mps = profile->erpm_per_mps();
    ASSERT(erpm_per_mps > 0, "erpm_per_mps positive");
    double speed = profile->speed_from_erpm(10000);
    ASSERT(speed > 0, "speed_from_erpm positive");
    double pct = profile->battery_percentage(72.0);
    ASSERT(pct > 0 && pct < 100, "battery_percentage mid-range");
    ASSERT_NEAR(profile->battery_percentage(84.0), 100.0, 0.01, "battery 100%");
    ASSERT_NEAR(profile->battery_percentage(60.0), 0.0, 0.01, "battery 0%");

    // FFI profile
    nd_profile_t* p = nd_profile_load(json, std::strlen(json));
    ASSERT(p != nullptr, "nd_profile_load non-null");
    ASSERT(std::strcmp(nd_profile_name(p), "Test Board") == 0, "ffi profile name");
    ASSERT_EQ(nd_profile_motor_pole_pairs(p), 15, "ffi pole pairs");
    ASSERT_NEAR(nd_profile_motor_resistance(p), 0.088, 1e-6, "ffi resistance");
    ASSERT_NEAR(nd_profile_battery_percentage(p, 72.0), pct, 0.01, "ffi battery pct");
    nd_profile_free(p);
}

// --- PacketDecoder tests ---
static void test_packet_decoder_single() {
    nosedive::PacketDecoder dec;

    uint8_t payload[] = {0x04, 0x01};
    auto pkt = nosedive::encode_packet(payload, 2);

    dec.feed(pkt.data(), pkt.size());
    ASSERT(dec.has_packet(), "decoder has packet after full feed");
    ASSERT_EQ(dec.packet_count(), 1u, "decoder count = 1");

    auto p = dec.pop();
    ASSERT_EQ(p.size(), 2u, "decoded payload size");
    ASSERT_EQ(p[0], 0x04, "decoded payload byte 0");
    ASSERT_EQ(p[1], 0x01, "decoded payload byte 1");
    ASSERT(!dec.has_packet(), "decoder empty after pop");
}

static void test_packet_decoder_chunked() {
    // Simulate BLE 20-byte MTU chunking
    nosedive::PacketDecoder dec;

    std::vector<uint8_t> payload(100, 0xAA);
    payload[0] = 0x14;
    auto pkt = nosedive::encode_packet(payload.data(), payload.size());

    // Feed in 20-byte chunks
    size_t offset = 0;
    while (offset < pkt.size()) {
        size_t chunk = std::min(size_t(20), pkt.size() - offset);
        dec.feed(pkt.data() + offset, chunk);
        offset += chunk;
    }

    ASSERT(dec.has_packet(), "decoder reassembled chunked packet");
    auto p = dec.pop();
    ASSERT_EQ(p.size(), 100u, "reassembled payload size");
    ASSERT_EQ(p[0], 0x14, "reassembled payload first byte");
}

static void test_packet_decoder_multiple() {
    nosedive::PacketDecoder dec;

    uint8_t p1[] = {0x04};
    uint8_t p2[] = {0x00};
    auto pkt1 = nosedive::encode_packet(p1, 1);
    auto pkt2 = nosedive::encode_packet(p2, 1);

    // Feed both packets in one call
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), pkt1.begin(), pkt1.end());
    combined.insert(combined.end(), pkt2.begin(), pkt2.end());
    dec.feed(combined.data(), combined.size());

    ASSERT_EQ(dec.packet_count(), 2u, "decoder found 2 packets");
    auto r1 = dec.pop();
    ASSERT_EQ(r1[0], 0x04, "first packet cmd");
    auto r2 = dec.pop();
    ASSERT_EQ(r2[0], 0x00, "second packet cmd");
}

// --- Refloat tests ---
static void test_refloat_command_builders() {
    auto cmd = nosedive::refloat::build_get_all_data(2);
    ASSERT_EQ(cmd.size(), 3u, "get_all_data size");
    ASSERT_EQ(cmd[0], 0x65, "magic byte");
    ASSERT_EQ(cmd[1], 10, "command ID = GetAllData");
    ASSERT_EQ(cmd[2], 2, "mode byte");

    auto rt_cmd = nosedive::refloat::build_get_rt_data();
    ASSERT_EQ(rt_cmd.size(), 2u, "get_rt_data size");
    ASSERT_EQ(rt_cmd[0], 0x65, "rt magic");
    ASSERT_EQ(rt_cmd[1], 1, "rt command ID");

    auto info_cmd = nosedive::refloat::build_info_request();
    ASSERT_EQ(info_cmd.size(), 3u, "info request size");
    ASSERT_EQ(info_cmd[0], 0x65, "info magic");
    ASSERT_EQ(info_cmd[1], 0, "info command ID");
    ASSERT_EQ(info_cmd[2], 2, "info version");
}

static void test_refloat_compat_decoders() {
    using namespace nosedive::refloat;

    // State compat: 0 = startup
    ASSERT(decode_state_compat(0) == RunState::Startup, "compat state 0 = startup");
    // State compat: 1-5 = running
    ASSERT(decode_state_compat(1) == RunState::Running, "compat state 1 = running");
    ASSERT(decode_state_compat(5) == RunState::Running, "compat state 5 = running");
    // State compat: 15 = disabled
    ASSERT(decode_state_compat(15) == RunState::Disabled, "compat state 15 = disabled");

    // Stop compat
    ASSERT(decode_stop_compat(6) == StopCondition::Pitch, "stop 6 = pitch");
    ASSERT(decode_stop_compat(9) == StopCondition::SwitchFull, "stop 9 = switch_full");
    ASSERT(decode_stop_compat(0) == StopCondition::None, "stop 0 = none");

    // SAT compat
    ASSERT(decode_sat_compat(0) == SAT::Centering, "sat 0 = centering");
    ASSERT(decode_sat_compat(2) == SAT::None, "sat 2 = none");
    ASSERT(decode_sat_compat(7) == SAT::PBSpeed, "sat 7 = pb_speed");
}

// --- BLE Transport test ---
static void test_ble_transport() {
    nosedive::BLETransport transport(20);

    // Track what gets sent
    std::vector<std::vector<uint8_t>> sent_chunks;
    transport.set_send_callback([&](const uint8_t* data, size_t len) {
        sent_chunks.emplace_back(data, data + len);
    });

    // Track received packets
    std::vector<std::vector<uint8_t>> received;
    transport.set_packet_callback([&](const uint8_t* payload, size_t len) {
        received.emplace_back(payload, payload + len);
    });

    // Send a payload — should chunk to MTU
    uint8_t payload[] = {0x04};
    ASSERT(transport.send_payload(payload, 1), "send_payload returns true");
    ASSERT(!sent_chunks.empty(), "send callback was called");

    // Now simulate receiving the same data back
    for (auto& chunk : sent_chunks) {
        transport.on_ble_receive(chunk.data(), chunk.size());
    }
    ASSERT_EQ(received.size(), 1u, "received one packet");
    ASSERT_EQ(received[0].size(), 1u, "received payload size");
    ASSERT_EQ(received[0][0], 0x04, "received payload content");
}

static void test_ffi_decoder() {
    nd_decoder_t* d = nd_decoder_create();
    ASSERT(d != nullptr, "decoder create non-null");

    uint8_t payload[] = {0x04};
    size_t pkt_len = 0;
    uint8_t* pkt = nd_encode_packet(payload, 1, &pkt_len);

    int count = nd_decoder_feed(d, pkt, pkt_len);
    ASSERT_EQ(count, 1, "ffi decoder feed returns 1");
    ASSERT_EQ(nd_decoder_count(d), 1u, "ffi decoder count");

    size_t out_len = 0;
    uint8_t* result = nd_decoder_pop(d, &out_len);
    ASSERT(result != nullptr, "ffi decoder pop non-null");
    ASSERT_EQ(out_len, 1u, "ffi decoder pop len");
    ASSERT_EQ(result[0], 0x04, "ffi decoder pop content");

    nd_free(result);
    nd_free(pkt);
    nd_decoder_destroy(d);
}

static void test_ffi_transport() {
    nd_transport_t* t = nd_transport_create(20);
    ASSERT(t != nullptr, "transport create non-null");

    // Set up send callback
    static std::vector<uint8_t> all_sent;
    all_sent.clear();
    nd_transport_set_send_callback(t, [](const uint8_t* data, size_t len, void*) {
        all_sent.insert(all_sent.end(), data, data + len);
    }, nullptr);

    // Set up receive callback
    static std::vector<uint8_t> last_payload;
    last_payload.clear();
    nd_transport_set_packet_callback(t, [](const uint8_t* payload, size_t len, void*) {
        last_payload.assign(payload, payload + len);
    }, nullptr);

    // Send a command
    ASSERT(nd_transport_send_command(t, 0x04), "transport send_command");
    ASSERT(!all_sent.empty(), "transport sent data");

    // Feed the sent data back as received
    nd_transport_receive(t, all_sent.data(), all_sent.size());
    ASSERT_EQ(last_payload.size(), 1u, "transport received payload size");
    ASSERT_EQ(last_payload[0], 0x04, "transport received payload content");

    nd_transport_destroy(t);
}

int main() {
    test_crc16();
    test_packet_roundtrip_short();
    test_packet_roundtrip_long();
    test_buffer_int16();
    test_buffer_int32();
    test_buffer_float16();
    test_buffer_float32_auto();
    test_buffer_string();
    test_ffi_packet();
    test_profile_load();
    test_packet_decoder_single();
    test_packet_decoder_chunked();
    test_packet_decoder_multiple();
    test_refloat_command_builders();
    test_refloat_compat_decoders();
    test_ble_transport();
    test_ffi_decoder();
    test_ffi_transport();

    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
