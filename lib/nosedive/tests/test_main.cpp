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

// --- Storage C++ round-trip ---
// --- FW version parsing ---
static void test_parse_fw_version() {
    // Build a realistic FW version payload:
    // [cmd=0][major=6][minor=5][hw="TestHW"\0][uuid:12][isPaired=0][fwTest=0][hwType=0]
    // [customConfigCount=1][hasPhaseFilters=0][qmlHW=0][qmlApp=0][nrfFlags=0][pkgName="Refloat"\0]
    std::vector<uint8_t> payload;
    payload.push_back(0x00); // cmd
    payload.push_back(6);    // major
    payload.push_back(5);    // minor
    // hw name
    for (char c : std::string("TestHW")) payload.push_back(c);
    payload.push_back(0);
    // uuid: 12 bytes
    for (int i = 0; i < 12; i++) payload.push_back(0xA0 + i);
    payload.push_back(0); // isPaired
    payload.push_back(0); // fwTest
    payload.push_back(0); // hwType = VESC
    payload.push_back(1); // customConfigCount
    payload.push_back(0); // hasPhaseFilters
    payload.push_back(0); // qmlHW
    payload.push_back(0); // qmlApp
    payload.push_back(0); // nrfFlags
    for (char c : std::string("Refloat")) payload.push_back(c);
    payload.push_back(0);

    auto fw = nosedive::parse_fw_version(payload.data(), payload.size());
    ASSERT(fw.has_value(), "parse_fw_version: success");
    ASSERT_EQ(fw->major, 6, "parse_fw_version: major");
    ASSERT_EQ(fw->minor, 5, "parse_fw_version: minor");
    ASSERT_EQ(fw->hw_name, "TestHW", "parse_fw_version: hw_name");
    ASSERT_EQ(fw->uuid.size(), 24u, "parse_fw_version: uuid length");
    ASSERT_EQ(fw->hw_type, nosedive::HWType::VESC, "parse_fw_version: hw_type");
    ASSERT_EQ(fw->custom_config_count, 1, "parse_fw_version: custom_config_count");
    ASSERT_EQ(fw->package_name, "Refloat", "parse_fw_version: package_name");

    // Express: hwType=3
    payload[payload.size() - 10] = 3; // hwType offset (approx)
    // Just test with hwType=3 in a fresh minimal payload
    std::vector<uint8_t> express = {0x00, 5, 3};
    express.push_back(0); // empty hw name
    for (int i = 0; i < 12; i++) express.push_back(0xBB);
    express.push_back(0); // isPaired
    express.push_back(0); // fwTest
    express.push_back(3); // hwType = VESCExpress
    auto fw2 = nosedive::parse_fw_version(express.data(), express.size());
    ASSERT(fw2.has_value(), "parse_fw_version express: success");
    ASSERT_EQ(fw2->hw_type, nosedive::HWType::VESCExpress, "parse_fw_version express: type");

    // Too short
    uint8_t bad[] = {0x00, 6};
    auto bad_result = nosedive::parse_fw_version(bad, 2);
    ASSERT(!bad_result.has_value(), "parse_fw_version: too short");
}

// --- CAN ping parsing ---
static void test_parse_ping_can() {
    uint8_t payload[] = {62, 10, 253}; // cmd=PingCAN, ids: 10 (BMS), 253 (Express)
    auto ids = nosedive::parse_ping_can(payload, 3);
    ASSERT_EQ(ids.size(), 2u, "parse_ping_can: 2 devices");
    ASSERT_EQ(ids[0], 10, "parse_ping_can: BMS");
    ASSERT_EQ(ids[1], 253, "parse_ping_can: Express");

    // Empty response
    uint8_t empty[] = {62};
    auto none = nosedive::parse_ping_can(empty, 1);
    ASSERT_EQ(none.size(), 0u, "parse_ping_can: empty");
}

// --- Refloat info parsing ---
static void test_parse_refloat_info() {
    // [cmd=36][magic=0x65][cmdId=0][version=2][major=1][minor=3][patch=0]
    // [name:20 bytes][suffix:20 bytes]
    std::vector<uint8_t> payload;
    payload.push_back(36);   // COMM_CUSTOM_APP_DATA
    payload.push_back(0x65); // magic
    payload.push_back(0x00); // CommandInfo
    payload.push_back(2);    // version
    payload.push_back(1);    // major
    payload.push_back(3);    // minor
    payload.push_back(0);    // patch
    // name: "Refloat" + padding
    std::string name = "Refloat";
    for (size_t i = 0; i < 20; i++) payload.push_back(i < name.size() ? name[i] : 0);
    // suffix: "beta" + padding
    std::string suffix = "beta";
    for (size_t i = 0; i < 20; i++) payload.push_back(i < suffix.size() ? suffix[i] : 0);

    auto info = nosedive::parse_refloat_info(payload.data(), payload.size());
    ASSERT(info.has_value(), "parse_refloat_info: success");
    ASSERT_EQ(info->major, 1, "parse_refloat_info: major");
    ASSERT_EQ(info->minor, 3, "parse_refloat_info: minor");
    ASSERT_EQ(info->patch, 0, "parse_refloat_info: patch");
    ASSERT_EQ(info->name, "Refloat", "parse_refloat_info: name");
    ASSERT_EQ(info->suffix, "beta", "parse_refloat_info: suffix");
    ASSERT_EQ(info->version_string(), "1.3.0-beta", "parse_refloat_info: version_string");
}

// --- Command builders ---
static void test_command_builders() {
    auto cmd = nosedive::build_command(nosedive::CommPacketID::GetValues);
    ASSERT_EQ(cmd.size(), 1u, "build_command: size");
    ASSERT_EQ(cmd[0], 4, "build_command: GetValues");

    auto can = nosedive::build_fw_version_request_can(253);
    ASSERT_EQ(can.size(), 3u, "build_fw_version_request_can: size");
    ASSERT_EQ(can[0], 34, "build_fw_version_request_can: ForwardCAN");
    ASSERT_EQ(can[1], 253, "build_fw_version_request_can: target_id");
    ASSERT_EQ(can[2], 0, "build_fw_version_request_can: FWVersion cmd");

    auto refloat = nosedive::build_refloat_info_request();
    ASSERT_EQ(refloat.size(), 3u, "build_refloat_info_request: size");
    ASSERT_EQ(refloat[0], 36, "build_refloat_info_request: CustomAppData");
    ASSERT_EQ(refloat[1], 0x65, "build_refloat_info_request: magic");
    ASSERT_EQ(refloat[2], 0x00, "build_refloat_info_request: CommandInfo");
}

// --- Speed / battery computations ---
static void test_computed_values() {
    // Speed from ERPM: erpm / (pole_pairs * 60 / wheel_circ)
    double speed = nosedive::speed_from_erpm(10000, 15, 0.8778);
    ASSERT(speed > 0, "speed_from_erpm: positive");
    ASSERT_NEAR(speed, 10000.0 / (15.0 * 60.0 / 0.8778), 0.001, "speed_from_erpm: correct");

    // Edge cases
    ASSERT_NEAR(nosedive::speed_from_erpm(0, 15, 0.88), 0.0, 0.001, "speed_from_erpm: zero erpm");
    ASSERT_NEAR(nosedive::speed_from_erpm(1000, 0, 0.88), 0.0, 0.001, "speed_from_erpm: zero poles");

    // Battery percent
    ASSERT_NEAR(nosedive::battery_percent(72.0, 60.0, 84.0), 50.0, 0.01, "battery_percent: 50%");
    ASSERT_NEAR(nosedive::battery_percent(84.0, 60.0, 84.0), 100.0, 0.01, "battery_percent: 100%");
    ASSERT_NEAR(nosedive::battery_percent(60.0, 60.0, 84.0), 0.0, 0.01, "battery_percent: 0%");
    ASSERT_NEAR(nosedive::battery_percent(90.0, 60.0, 84.0), 100.0, 0.01, "battery_percent: clamped max");
    ASSERT_NEAR(nosedive::battery_percent(50.0, 60.0, 84.0), 0.0, 0.01, "battery_percent: clamped min");
}

static void test_storage_roundtrip() {
    nosedive::AppData data;

    nosedive::Board b;
    b.id = "board-uuid-123";
    b.name = "My OneWheel";
    b.ble_name = "OW-1234";
    b.hw_name = "Little FOCer V3.1";
    b.fw_major = 6;
    b.fw_minor = 2;
    b.motor_pole_pairs = 15;
    b.wheel_circumference_m = 0.92;
    b.battery_series_cells = 20;
    b.battery_voltage_min = 60.0;
    b.battery_voltage_max = 84.0;
    b.lifetime_distance_m = 12345.67;
    b.ride_count = 42;
    b.wizard_complete = true;
    b.refloat_version = "1.3.0";
    b.last_connected = 1700000000;
    b.active_profile_id = "profile-uuid-1";
    data.boards.push_back(b);

    nosedive::RiderProfile p;
    p.id = "profile-uuid-1";
    p.name = "Chill";
    p.icon = "leaf";
    p.is_built_in = false;
    p.created_at = 1700000000;
    p.modified_at = 1700000100;
    p.responsiveness = 3.0;
    p.stability = 7.0;
    p.carving = 5.5;
    p.braking = 4.0;
    p.safety = 8.0;
    p.agility = 6.0;
    p.footpad_sensitivity = 5.0;
    p.disengage_speed = 3.5;
    data.rider_profiles.push_back(p);

    data.active_profile_id = "profile-uuid-1";

    // Serialize to JSON and back
    std::string json = nosedive::app_data_to_json(data);
    ASSERT(!json.empty(), "storage: JSON not empty");

    auto loaded = nosedive::app_data_from_json(json);
    ASSERT_EQ(loaded.boards.size(), 1u, "storage: 1 board");
    ASSERT_EQ(loaded.rider_profiles.size(), 1u, "storage: 1 profile");
    ASSERT_EQ(loaded.active_profile_id, "profile-uuid-1", "storage: active profile id");

    auto& lb = loaded.boards[0];
    ASSERT_EQ(lb.id, "board-uuid-123", "storage: board id");
    ASSERT_EQ(lb.name, "My OneWheel", "storage: board name");
    ASSERT_EQ(lb.ble_name, "OW-1234", "storage: board ble_name");
    ASSERT_EQ(lb.hw_name, "Little FOCer V3.1", "storage: board hw_name");
    ASSERT_EQ(lb.fw_major, 6, "storage: board fw_major");
    ASSERT_EQ(lb.fw_minor, 2, "storage: board fw_minor");
    ASSERT_EQ(lb.motor_pole_pairs, 15, "storage: board motor_pole_pairs");
    ASSERT_NEAR(lb.wheel_circumference_m, 0.92, 0.001, "storage: board wheel_circ");
    ASSERT_EQ(lb.battery_series_cells, 20, "storage: board battery_cells");
    ASSERT_NEAR(lb.battery_voltage_min, 60.0, 0.01, "storage: board batt_min");
    ASSERT_NEAR(lb.battery_voltage_max, 84.0, 0.01, "storage: board batt_max");
    ASSERT_NEAR(lb.lifetime_distance_m, 12345.67, 0.01, "storage: board distance");
    ASSERT_EQ(lb.ride_count, 42, "storage: board ride_count");
    ASSERT(lb.wizard_complete, "storage: board wizard_complete");
    ASSERT_EQ(lb.refloat_version, "1.3.0", "storage: board refloat_version");
    ASSERT_EQ(lb.last_connected, 1700000000, "storage: board last_connected");
    ASSERT_EQ(lb.active_profile_id, "profile-uuid-1", "storage: board active_profile_id");

    auto& lp = loaded.rider_profiles[0];
    ASSERT_EQ(lp.id, "profile-uuid-1", "storage: profile id");
    ASSERT_EQ(lp.name, "Chill", "storage: profile name");
    ASSERT_EQ(lp.icon, "leaf", "storage: profile icon");
    ASSERT(!lp.is_built_in, "storage: profile not built-in");
    ASSERT_NEAR(lp.responsiveness, 3.0, 0.01, "storage: profile responsiveness");
    ASSERT_NEAR(lp.stability, 7.0, 0.01, "storage: profile stability");
    ASSERT_NEAR(lp.footpad_sensitivity, 5.0, 0.01, "storage: profile footpad_sens");
    ASSERT_NEAR(lp.disengage_speed, 3.5, 0.01, "storage: profile disengage_speed");
}

// --- Engine FFI round-trip ---
static void test_engine_ffi() {
    const char* path = "/tmp/nosedive_test_engine.json";
    std::remove(path); // clean slate

    auto* e = nd_engine_create(path);
    ASSERT(e != nullptr, "engine: create");

    // Save a board via engine
    nd_board_t cb = {};
    std::strncpy(cb.id, "ffi-board-1", sizeof(cb.id) - 1);
    std::strncpy(cb.name, "Test Board", sizeof(cb.name) - 1);
    cb.fw_major = 6;
    cb.fw_minor = 5;
    cb.motor_pole_pairs = 15;
    cb.wheel_circumference_m = 0.88;
    cb.battery_series_cells = 20;
    cb.battery_voltage_min = 60.0;
    cb.battery_voltage_max = 84.0;
    cb.wizard_complete = true;
    nd_engine_save_board(e, cb);
    ASSERT_EQ(nd_engine_board_count(e), 1u, "engine: 1 board");

    // Save a profile via engine
    nd_rider_profile_t cp = {};
    std::strncpy(cp.id, "ffi-profile-1", sizeof(cp.id) - 1);
    std::strncpy(cp.name, "Flow", sizeof(cp.name) - 1);
    std::strncpy(cp.icon, "wind", sizeof(cp.icon) - 1);
    cp.responsiveness = 5.0;
    cp.stability = 5.0;
    nd_engine_save_profile(e, cp);
    ASSERT_EQ(nd_engine_profile_count(e), 1u, "engine: 1 profile");

    nd_engine_set_active_profile_id(e, "ffi-profile-1");
    nd_engine_destroy(e);

    // Reload from disk
    auto* e2 = nd_engine_create(path);
    ASSERT(e2 != nullptr, "engine: reload");
    ASSERT_EQ(nd_engine_board_count(e2), 1u, "engine: reloaded 1 board");
    ASSERT_EQ(nd_engine_profile_count(e2), 1u, "engine: reloaded 1 profile");

    nd_board_t lb = nd_engine_get_board(e2, 0);
    ASSERT_EQ(std::string(lb.id), "ffi-board-1", "engine: board id");
    ASSERT_EQ(std::string(lb.name), "Test Board", "engine: board name");
    ASSERT_EQ(lb.fw_major, 6, "engine: board fw_major");
    ASSERT(lb.wizard_complete, "engine: board wizard_complete");
    ASSERT_NEAR(lb.wheel_circumference_m, 0.88, 0.001, "engine: board wheel_circ");

    nd_rider_profile_t lp = nd_engine_get_profile(e2, 0);
    ASSERT_EQ(std::string(lp.id), "ffi-profile-1", "engine: profile id");
    ASSERT_EQ(std::string(lp.name), "Flow", "engine: profile name");
    ASSERT_NEAR(lp.responsiveness, 5.0, 0.01, "engine: profile responsiveness");

    auto* aid = nd_engine_active_profile_id(e2);
    ASSERT(aid != nullptr, "engine: active profile id not null");
    ASSERT_EQ(std::string(aid), "ffi-profile-1", "engine: active profile id");

    nd_engine_destroy(e2);
    std::remove(path);
}

// --- Engine payload handling ---
static void test_engine_payload() {
    const char* path = "/tmp/nosedive_test_engine_payload.json";
    std::remove(path);

    auto* e = nd_engine_create(path);

    // Track sent payloads
    static std::vector<std::vector<uint8_t>> sent;
    sent.clear();
    nd_engine_set_send_callback(e, [](const uint8_t* data, size_t len, void*) {
        sent.emplace_back(data, data + len);
    }, nullptr);

    // Simulate connection — should trigger FW, CAN, Refloat requests
    nd_engine_on_connected(e);
    ASSERT(sent.size() >= 3, "engine: sends discovery on connect");

    // Feed a FW version response
    // [cmd=0][major=6][minor=5][hw="TestHW"\0][uuid:12 bytes]
    std::vector<uint8_t> fw_payload;
    fw_payload.push_back(0x00); // COMM_FW_VERSION
    fw_payload.push_back(6);    // major
    fw_payload.push_back(5);    // minor
    const char* hw = "TestHW";
    for (size_t i = 0; hw[i]; i++) fw_payload.push_back(hw[i]);
    fw_payload.push_back(0); // null terminator
    // UUID: 12 bytes
    for (int i = 0; i < 12; i++) fw_payload.push_back(0xA0 + i);

    nd_engine_handle_payload(e, fw_payload.data(), fw_payload.size());

    ASSERT(nd_engine_has_active_board(e), "engine: has active board after FW");

    nd_fw_version_t fw = nd_engine_get_main_fw(e);
    ASSERT_EQ(fw.major, 6, "engine: fw major");
    ASSERT_EQ(fw.minor, 5, "engine: fw minor");
    ASSERT_EQ(std::string(fw.hw_name), "TestHW", "engine: fw hw_name");

    // Telemetry should be zeroed initially
    nd_telemetry_t tel = nd_engine_get_telemetry(e);
    ASSERT_NEAR(tel.speed, 0.0, 0.001, "engine: initial speed 0");

    // Disconnect
    nd_engine_on_disconnected(e);
    ASSERT(!nd_engine_has_active_board(e), "engine: no board after disconnect");

    nd_engine_destroy(e);
    std::remove(path);
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
    test_parse_fw_version();
    test_parse_ping_can();
    test_parse_refloat_info();
    test_command_builders();
    test_computed_values();
    test_storage_roundtrip();
    test_engine_ffi();
    test_engine_payload();

    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
