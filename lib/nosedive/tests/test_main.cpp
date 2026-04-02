// Minimal test runner — no dependencies
#include "nosedive/nosedive.hpp"
#include "nosedive/ffi.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "test_helpers.hpp"
// --- CRC tests ---
static void test_crc16() {
    // Known VESC CRC: empty-ish payload
    uint8_t data[] = {0x04}; // COMM_GET_VALUES
    uint16_t crc = vesc::crc16(data, 1);
    // Just verify it returns something non-zero and deterministic
    ASSERT(crc != 0, "crc16 non-zero for non-empty input");
    ASSERT_EQ(vesc::crc16(data, 1), crc, "crc16 deterministic");

    // (nd_crc16 removed from FFI — internal only)
}

// --- Packet encode/decode round-trip ---
static void test_packet_roundtrip_short() {
    uint8_t payload[] = {0x04};
    auto pkt = vesc::encode_packet(payload, 1);
    ASSERT(!pkt.empty(), "encode_packet short non-empty");
    ASSERT_EQ(pkt[0], 0x02, "short packet starts with 0x02");
    ASSERT_EQ(pkt[1], 1, "short packet length byte");
    ASSERT_EQ(pkt.back(), 0x03, "packet ends with 0x03");

    auto decoded = vesc::decode_packet(pkt.data(), pkt.size());
    ASSERT(decoded.has_value(), "decode_packet succeeds");
    ASSERT_EQ(decoded->payload.size(), 1u, "decoded payload size");
    ASSERT_EQ(decoded->payload[0], 0x04, "decoded payload content");
    ASSERT_EQ(decoded->bytes_consumed, pkt.size(), "consumed all bytes");
}

static void test_packet_roundtrip_long() {
    // Create a 300-byte payload (long packet)
    std::vector<uint8_t> payload(300, 0xAB);
    payload[0] = 0x14; // some command
    auto pkt = vesc::encode_packet(payload.data(), payload.size());
    ASSERT(!pkt.empty(), "encode_packet long non-empty");
    ASSERT_EQ(pkt[0], 0x03, "long packet starts with 0x03");

    auto decoded = vesc::decode_packet(pkt.data(), pkt.size());
    ASSERT(decoded.has_value(), "decode long packet succeeds");
    ASSERT_EQ(decoded->payload.size(), 300u, "decoded long payload size");
    ASSERT_EQ(decoded->payload[0], 0x14, "decoded long payload first byte");
}

// --- Buffer helpers ---
static void test_buffer_int16() {
    vesc::Buffer buf;
    buf.append_int16(-1234);
    buf.append_int16(5678);

    vesc::Buffer reader(buf.vec());
    ASSERT_EQ(reader.read_int16(), -1234, "int16 round-trip negative");
    ASSERT_EQ(reader.read_int16(), 5678, "int16 round-trip positive");
}

static void test_buffer_int32() {
    vesc::Buffer buf;
    buf.append_int32(-123456);
    buf.append_uint32(0xDEADBEEF);

    vesc::Buffer reader(buf.vec());
    ASSERT_EQ(reader.read_int32(), -123456, "int32 round-trip");
    ASSERT_EQ(reader.read_uint32(), 0xDEADBEEFu, "uint32 round-trip");
}

static void test_buffer_float16() {
    vesc::Buffer buf;
    buf.append_float16(25.5, 10.0);

    vesc::Buffer reader(buf.vec());
    ASSERT_NEAR(reader.read_float16(10.0), 25.5, 0.11, "float16 round-trip");
}

static void test_buffer_float32_auto() {
    vesc::Buffer buf;
    double values[] = {0.0, 1.0, -1.0, 3.14159, 1e-7, 1e7};
    for (double v : values) {
        buf.append_float32_auto(v);
    }

    vesc::Buffer reader(buf.vec());
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
    vesc::Buffer buf;
    buf.append_string("hello");
    buf.append_string("world");

    vesc::Buffer reader(buf.vec());
    ASSERT(reader.read_string() == "hello", "string round-trip 1");
    ASSERT(reader.read_string() == "world", "string round-trip 2");
}

// (test_ffi_packet removed — nd_encode/decode_packet removed from FFI)

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
    vesc::PacketDecoder dec;

    uint8_t payload[] = {0x04, 0x01};
    auto pkt = vesc::encode_packet(payload, 2);

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
    vesc::PacketDecoder dec;

    std::vector<uint8_t> payload(100, 0xAA);
    payload[0] = 0x14;
    auto pkt = vesc::encode_packet(payload.data(), payload.size());

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
    vesc::PacketDecoder dec;

    uint8_t p1[] = {0x04};
    uint8_t p2[] = {0x00};
    auto pkt1 = vesc::encode_packet(p1, 1);
    auto pkt2 = vesc::encode_packet(p2, 1);

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
    auto cmd = vesc::refloat::build_get_all_data(2);
    ASSERT_EQ(cmd.size(), 3u, "get_all_data size");
    ASSERT_EQ(cmd[0], 0x65, "magic byte");
    ASSERT_EQ(cmd[1], 10, "command ID = GetAllData");
    ASSERT_EQ(cmd[2], 2, "mode byte");

    auto rt_cmd = vesc::refloat::build_get_rt_data();
    ASSERT_EQ(rt_cmd.size(), 2u, "get_rt_data size");
    ASSERT_EQ(rt_cmd[0], 0x65, "rt magic");
    ASSERT_EQ(rt_cmd[1], 1, "rt command ID");

    auto info_cmd = vesc::refloat::build_info_request();
    ASSERT_EQ(info_cmd.size(), 3u, "info request size");
    ASSERT_EQ(info_cmd[0], 0x65, "info magic");
    ASSERT_EQ(info_cmd[1], 0, "info command ID");
    ASSERT_EQ(info_cmd[2], 2, "info version");
}

static void test_refloat_compat_decoders() {
    using namespace vesc::refloat;

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

// (BLETransport and nd_transport tests removed — transport is now internal to engine)

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

    auto fw = vesc::FWVersion::Response::decode(payload.data(), payload.size());
    ASSERT(fw.has_value(), "parse_fw_version: success");
    ASSERT_EQ(fw->major, 6, "parse_fw_version: major");
    ASSERT_EQ(fw->minor, 5, "parse_fw_version: minor");
    ASSERT_EQ(fw->hw_name, "TestHW", "parse_fw_version: hw_name");
    ASSERT_EQ(fw->uuid.size(), 24u, "parse_fw_version: uuid length");
    ASSERT_EQ(fw->hw_type, vesc::HWType::VESC, "parse_fw_version: hw_type");
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
    auto fw2 = vesc::FWVersion::Response::decode(express.data(), express.size());
    ASSERT(fw2.has_value(), "parse_fw_version express: success");
    ASSERT_EQ(fw2->hw_type, vesc::HWType::VESCExpress, "parse_fw_version express: type");

    // Too short
    uint8_t bad[] = {0x00, 6};
    auto bad_result = vesc::FWVersion::Response::decode(bad, 2);
    ASSERT(!bad_result.has_value(), "parse_fw_version: too short");
}

// --- CAN ping parsing ---
static void test_parse_ping_can() {
    uint8_t payload[] = {62, 10, 253}; // cmd=PingCAN, ids: 10 (BMS), 253 (Express)
    auto r = vesc::PingCAN::Response::decode(payload, 3);
    ASSERT(r.has_value(), "parse_ping_can: decoded");
    ASSERT_EQ(r->device_ids.size(), 2u, "parse_ping_can: 2 devices");
    ASSERT_EQ(r->device_ids[0], 10, "parse_ping_can: BMS");
    ASSERT_EQ(r->device_ids[1], 253, "parse_ping_can: Express");

    // Empty response
    uint8_t empty[] = {62};
    auto none = vesc::PingCAN::Response::decode(empty, 1);
    ASSERT(none.has_value(), "parse_ping_can: empty decoded");
    ASSERT_EQ(none->device_ids.size(), 0u, "parse_ping_can: empty");
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

    auto info = vesc::parse_refloat_info(payload.data(), payload.size());
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
    auto cmd = std::vector<uint8_t>{static_cast<uint8_t>(vesc::CommPacketID::GetValues)};
    ASSERT_EQ(cmd.size(), 1u, "build_command: size");
    ASSERT_EQ(cmd[0], 4, "build_command: GetValues");

    auto can = vesc::ForwardCAN::Request{.target_id = 253, .inner_payload = vesc::FWVersion::Request{}.encode()}.encode();
    ASSERT_EQ(can.size(), 3u, "ForwardCAN encode: size");
    ASSERT_EQ(can[0], 34, "ForwardCAN encode: cmd");
    ASSERT_EQ(can[1], 253, "ForwardCAN encode: target_id");
    ASSERT_EQ(can[2], 0, "build_fw_version_request_can: FWVersion cmd");

    auto refloat = vesc::build_refloat_info_request();
    ASSERT_EQ(refloat.size(), 3u, "build_refloat_info_request: size");
    ASSERT_EQ(refloat[0], 36, "build_refloat_info_request: CustomAppData");
    ASSERT_EQ(refloat[1], 0x65, "build_refloat_info_request: magic");
    ASSERT_EQ(refloat[2], 0x00, "build_refloat_info_request: CommandInfo");
}

// --- Speed / battery computations ---
static void test_computed_values() {
    // Speed from ERPM: erpm / (pole_pairs * 60 / wheel_circ)
    double speed = vesc::speed_from_erpm(10000, 15, 0.8778);
    ASSERT(speed > 0, "speed_from_erpm: positive");
    ASSERT_NEAR(speed, 10000.0 / (15.0 * 60.0 / 0.8778), 0.001, "speed_from_erpm: correct");

    // Edge cases
    ASSERT_NEAR(vesc::speed_from_erpm(0, 15, 0.88), 0.0, 0.001, "speed_from_erpm: zero erpm");
    ASSERT_NEAR(vesc::speed_from_erpm(1000, 0, 0.88), 0.0, 0.001, "speed_from_erpm: zero poles");

    // Battery percent
    ASSERT_NEAR(vesc::battery_percent(72.0, 60.0, 84.0), 50.0, 0.01, "battery_percent: 50%");
    ASSERT_NEAR(vesc::battery_percent(84.0, 60.0, 84.0), 100.0, 0.01, "battery_percent: 100%");
    ASSERT_NEAR(vesc::battery_percent(60.0, 60.0, 84.0), 0.0, 0.01, "battery_percent: 0%");
    ASSERT_NEAR(vesc::battery_percent(90.0, 60.0, 84.0), 100.0, 0.01, "battery_percent: clamped max");
    ASSERT_NEAR(vesc::battery_percent(50.0, 60.0, 84.0), 0.0, 0.01, "battery_percent: clamped min");
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

// --- Engine receive_bytes + domain callbacks ---
static void test_engine_payload() {
    const char* path = "/tmp/nosedive_test_engine_payload.json";
    std::remove(path);

    auto* e = nd_engine_create(path);

    // Track writes (framed packets out)
    static std::vector<uint8_t> written;
    written.clear();
    nd_engine_set_write_callback(e, [](const uint8_t* data, size_t len, void*) {
        written.insert(written.end(), data, data + len);
    }, nullptr);

    // Track board callback
    static bool got_board = false;
    static nd_board_event_t last_board = {};
    got_board = false;
    nd_engine_set_board_callback(e, [](nd_board_event_t board, void*) {
        got_board = true;
        last_board = board;
    }, nullptr);

    // Simulate connection — should write discovery packets
    nd_engine_on_connected(e, 512);
    ASSERT(!written.empty(), "engine: writes discovery on connect");

    // Build a FW_VERSION response and frame it as a VESC packet
    std::vector<uint8_t> fw_payload;
    fw_payload.push_back(0x00); // COMM_FW_VERSION
    fw_payload.push_back(6);    // major
    fw_payload.push_back(5);    // minor
    const char* hw = "TestHW";
    for (size_t i = 0; hw[i]; i++) fw_payload.push_back(hw[i]);
    fw_payload.push_back(0); // null terminator
    for (int i = 0; i < 12; i++) fw_payload.push_back(0xA0 + i); // UUID

    auto framed = vesc::encode_packet(fw_payload.data(), fw_payload.size());
    ASSERT(!framed.empty(), "engine: framed FW response");

    // Feed raw bytes — engine decodes internally
    nd_engine_receive_bytes(e, framed.data(), framed.size());

    ASSERT(got_board, "engine: board callback fired");
    ASSERT_EQ(last_board.fw_major, 6, "engine: fw major");
    ASSERT_EQ(last_board.fw_minor, 5, "engine: fw minor");
    ASSERT_EQ(std::string(last_board.hw_name), "TestHW", "engine: hw_name");
    ASSERT(last_board.show_wizard, "engine: wizard for unknown board");

    // Disconnect
    nd_engine_on_disconnected(e);

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
    test_profile_load();
    test_packet_decoder_single();
    test_packet_decoder_chunked();
    test_packet_decoder_multiple();
    test_refloat_command_builders();
    test_refloat_compat_decoders();
    // (transport tests removed — transport is internal to engine)
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
