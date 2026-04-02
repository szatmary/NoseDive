#include "nosedive/nosedive.hpp"
#include "nosedive/ffi.h"
#include <cstdlib>
#include <cstring>
#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// --- CRC tests ---
TEST_CASE("CRC16 is non-zero and deterministic for non-empty input", "[crc]") {
    uint8_t data[] = {0x04}; // COMM_GET_VALUES
    uint16_t crc = vesc::crc16(data, 1);
    // Just verify it returns something non-zero and deterministic
    REQUIRE(crc != 0);
    REQUIRE(vesc::crc16(data, 1) == crc);

    // (nd_crc16 removed from FFI — internal only)
}

// --- Packet encode/decode round-trip ---
TEST_CASE("Packet encode/decode round-trip for short payload", "[packet]") {
    uint8_t payload[] = {0x04};
    auto pkt = vesc::encode_packet(payload, 1);
    REQUIRE(!pkt.empty());
    REQUIRE(pkt[0] == 0x02);
    REQUIRE(pkt[1] == 1);
    REQUIRE(pkt.back() == 0x03);

    auto decoded = vesc::decode_packet(pkt.data(), pkt.size());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->payload.size() == 1u);
    REQUIRE(decoded->payload[0] == 0x04);
    REQUIRE(decoded->bytes_consumed == pkt.size());
}

TEST_CASE("Packet encode/decode round-trip for long payload", "[packet]") {
    // Create a 300-byte payload (long packet)
    std::vector<uint8_t> payload(300, 0xAB);
    payload[0] = 0x14; // some command
    auto pkt = vesc::encode_packet(payload.data(), payload.size());
    REQUIRE(!pkt.empty());
    REQUIRE(pkt[0] == 0x03);

    auto decoded = vesc::decode_packet(pkt.data(), pkt.size());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->payload.size() == 300u);
    REQUIRE(decoded->payload[0] == 0x14);
}

// --- Buffer helpers ---
TEST_CASE("Buffer int16 round-trip", "[buffer]") {
    vesc::Buffer buf;
    buf.append_int16(-1234);
    buf.append_int16(5678);

    vesc::Buffer reader(buf.vec());
    REQUIRE(reader.read_int16() == -1234);
    REQUIRE(reader.read_int16() == 5678);
}

TEST_CASE("Buffer int32 and uint32 round-trip", "[buffer]") {
    vesc::Buffer buf;
    buf.append_int32(-123456);
    buf.append_uint32(0xDEADBEEF);

    vesc::Buffer reader(buf.vec());
    REQUIRE(reader.read_int32() == -123456);
    REQUIRE(reader.read_uint32() == 0xDEADBEEFu);
}

TEST_CASE("Buffer float16 round-trip", "[buffer]") {
    vesc::Buffer buf;
    buf.append_float16(25.5, 10.0);

    vesc::Buffer reader(buf.vec());
    REQUIRE_THAT(reader.read_float16(10.0), Catch::Matchers::WithinAbs(25.5, 0.11));
}

TEST_CASE("Buffer float32_auto round-trip", "[buffer]") {
    vesc::Buffer buf;
    double values[] = {0.0, 1.0, -1.0, 3.14159, 1e-7, 1e7};
    for (double v : values) {
        buf.append_float32_auto(v);
    }

    vesc::Buffer reader(buf.vec());
    for (double v : values) {
        double got = reader.read_float32_auto();
        if (v == 0.0) {
            REQUIRE(got == 0.0);
        } else {
            double rel = std::fabs((got - v) / v);
            REQUIRE(rel < 1e-6);
        }
    }
}

TEST_CASE("Buffer string round-trip", "[buffer]") {
    vesc::Buffer buf;
    buf.append_string("hello");
    buf.append_string("world");

    vesc::Buffer reader(buf.vec());
    REQUIRE(reader.read_string() == "hello");
    REQUIRE(reader.read_string() == "world");
}

// (test_ffi_packet removed — nd_encode/decode_packet removed from FFI)

// --- Profile loading ---
TEST_CASE("Profile load from JSON", "[profile]") {
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
    REQUIRE(profile.has_value());
    REQUIRE(profile->name == "Test Board");
    REQUIRE(profile->manufacturer == "TestCo");
    REQUIRE(profile->motor.pole_pairs == 15);
    REQUIRE_THAT(profile->motor.resistance, Catch::Matchers::WithinAbs(0.088, 1e-6));
    REQUIRE_THAT(profile->motor.inductance, Catch::Matchers::WithinAbs(0.000233, 1e-9));
    REQUIRE_THAT(profile->motor.flux_linkage, Catch::Matchers::WithinAbs(0.028, 1e-6));
    REQUIRE(profile->battery.series_cells == 20);
    REQUIRE_THAT(profile->battery.capacity_wh, Catch::Matchers::WithinAbs(576.0, 0.1));
    REQUIRE(static_cast<int>(profile->motor.hall_sensor_table.size()) == 8);
    REQUIRE(profile->motor.hall_sensor_table[0] == 255);
    REQUIRE(profile->motor.hall_sensor_table[1] == 1);

    // Computed values
    double erpm_per_mps = profile->erpm_per_mps();
    REQUIRE(erpm_per_mps > 0);
    double speed = profile->speed_from_erpm(10000);
    REQUIRE(speed > 0);
    double pct = profile->battery_percentage(72.0);
    REQUIRE(pct > 0);
    REQUIRE(pct < 100);
    REQUIRE_THAT(profile->battery_percentage(84.0), Catch::Matchers::WithinAbs(100.0, 0.01));
    REQUIRE_THAT(profile->battery_percentage(60.0), Catch::Matchers::WithinAbs(0.0, 0.01));
}

// --- PacketDecoder tests ---
TEST_CASE("PacketDecoder reassembles a single full packet", "[packet]") {
    vesc::PacketDecoder dec;
    uint8_t payload[] = {0x04, 0x01};
    auto pkt = vesc::encode_packet(payload, 2);

    dec.feed(pkt.data(), pkt.size());
    REQUIRE(dec.has_packet());
    REQUIRE(dec.packet_count() == 1u);

    auto p = dec.pop();
    REQUIRE(p.size() == 2u);
    REQUIRE(p[0] == 0x04);
    REQUIRE(p[1] == 0x01);
    REQUIRE(!dec.has_packet());
}

TEST_CASE("PacketDecoder reassembles a packet fed in 20-byte chunks", "[packet]") {
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

    REQUIRE(dec.has_packet());
    auto p = dec.pop();
    REQUIRE(p.size() == 100u);
    REQUIRE(p[0] == 0x14);
}

TEST_CASE("PacketDecoder handles multiple packets in one feed", "[packet]") {
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

    REQUIRE(dec.packet_count() == 2u);
    auto r1 = dec.pop();
    REQUIRE(r1[0] == 0x04);
    auto r2 = dec.pop();
    REQUIRE(r2[0] == 0x00);
}

// --- Refloat tests ---
TEST_CASE("Refloat command builders produce correctly encoded payloads", "[refloat]") {
    auto cmd = vesc::refloat::build_get_all_data(2);
    REQUIRE(cmd.size() == 3u);
    REQUIRE(cmd[0] == 0x65);
    REQUIRE(cmd[1] == 10);
    REQUIRE(cmd[2] == 2);

    auto rt_cmd = vesc::refloat::build_get_rt_data();
    REQUIRE(rt_cmd.size() == 2u);
    REQUIRE(rt_cmd[0] == 0x65);
    REQUIRE(rt_cmd[1] == 1);

    auto info_cmd = vesc::refloat::build_info_request();
    REQUIRE(info_cmd.size() == 3u);
    REQUIRE(info_cmd[0] == 0x65);
    REQUIRE(info_cmd[1] == 0);
    REQUIRE(info_cmd[2] == 2);
}

TEST_CASE("Refloat compatibility decoders map legacy values correctly", "[refloat]") {
    using namespace vesc::refloat;

    // State compat: 0 = startup
    REQUIRE(decode_state_compat(0) == RunState::Startup);
    // State compat: 1-5 = running
    REQUIRE(decode_state_compat(1) == RunState::Running);
    REQUIRE(decode_state_compat(5) == RunState::Running);
    // State compat: 15 = disabled
    REQUIRE(decode_state_compat(15) == RunState::Disabled);

    // Stop compat
    REQUIRE(decode_stop_compat(6) == StopCondition::Pitch);
    REQUIRE(decode_stop_compat(9) == StopCondition::SwitchFull);
    REQUIRE(decode_stop_compat(0) == StopCondition::None);

    // SAT compat
    REQUIRE(decode_sat_compat(0) == SAT::Centering);
    REQUIRE(decode_sat_compat(2) == SAT::None);
    REQUIRE(decode_sat_compat(7) == SAT::PBSpeed);
}

// (BLETransport and nd_transport tests removed — transport is now internal to engine)

// --- Storage C++ round-trip ---
// --- FW version parsing ---
TEST_CASE("FW version response decodes correctly", "[fw]") {
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
    REQUIRE(fw.has_value());
    REQUIRE(fw->major == 6);
    REQUIRE(fw->minor == 5);
    REQUIRE(fw->hw_name == "TestHW");
    REQUIRE(fw->uuid.size() == 24u);
    REQUIRE(fw->hw_type == vesc::HWType::VESC);
    REQUIRE(fw->custom_config_count == 1);
    REQUIRE(fw->package_name == "Refloat");

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
    REQUIRE(fw2.has_value());
    REQUIRE(fw2->hw_type == vesc::HWType::VESCExpress);

    // Too short
    uint8_t bad[] = {0x00, 6};
    auto bad_result = vesc::FWVersion::Response::decode(bad, 2);
    REQUIRE(!bad_result.has_value());
}

// --- CAN ping parsing ---
TEST_CASE("PingCAN response decodes device IDs", "[can]") {
    uint8_t payload[] = {62, 10, 253}; // cmd=PingCAN, ids: 10 (BMS), 253 (Express)
    auto r = vesc::PingCAN::Response::decode(payload, 3);
    REQUIRE(r.has_value());
    REQUIRE(r->device_ids.size() == 2u);
    REQUIRE(r->device_ids[0] == 10);
    REQUIRE(r->device_ids[1] == 253);

    // Empty response
    uint8_t empty[] = {62};
    auto none = vesc::PingCAN::Response::decode(empty, 1);
    REQUIRE(none.has_value());
    REQUIRE(none->device_ids.size() == 0u);
}

// --- Refloat info parsing ---
TEST_CASE("Refloat info response decodes version and name fields", "[refloat]") {
    // Version 2 format: [cmd][magic][cmdId][version][flags][name:20][major][minor][patch][suffix:20]
    std::vector<uint8_t> payload;
    payload.push_back(36);   // COMM_CUSTOM_APP_DATA
    payload.push_back(0x65); // magic
    payload.push_back(0x00); // CommandInfo
    payload.push_back(2);    // version
    payload.push_back(0);    // flags
    // name: "Refloat" + padding to 20 bytes
    std::string name = "Refloat";
    for (size_t i = 0; i < 20; i++) payload.push_back(i < name.size() ? name[i] : 0);
    payload.push_back(1);    // major
    payload.push_back(3);    // minor
    payload.push_back(0);    // patch
    // suffix: "beta" + padding to 20 bytes
    std::string suffix = "beta";
    for (size_t i = 0; i < 20; i++) payload.push_back(i < suffix.size() ? suffix[i] : 0);

    auto info = vesc::parse_refloat_info(payload.data(), payload.size());
    REQUIRE(info.has_value());
    REQUIRE(info->major == 1);
    REQUIRE(info->minor == 3);
    REQUIRE(info->patch == 0);
    REQUIRE(info->name == "Refloat");
    REQUIRE(info->suffix == "beta");
    REQUIRE(info->version_string() == "1.3.0-beta");
}

// --- Command builders ---
TEST_CASE("Command builders produce correctly encoded byte sequences", "[packet]") {
    auto cmd = std::vector<uint8_t>{static_cast<uint8_t>(vesc::CommPacketID::GetValues)};
    REQUIRE(cmd.size() == 1u);
    REQUIRE(cmd[0] == 4);

    auto can = vesc::ForwardCAN::Request{.target_id = 253, .inner_payload = vesc::FWVersion::Request{}.encode()}.encode();
    REQUIRE(can.size() == 3u);
    REQUIRE(can[0] == 34);
    REQUIRE(can[1] == 253);
    REQUIRE(can[2] == 0);

    auto refloat = vesc::build_refloat_info_request();
    REQUIRE(refloat.size() == 4u);
    REQUIRE(refloat[0] == 36);
    REQUIRE(refloat[1] == 0x65);
    REQUIRE(refloat[2] == 0x00);
    REQUIRE(refloat[3] == 0x02);
}

// --- Speed / battery computations ---
TEST_CASE("Speed from ERPM and battery percentage computations", "[profile]") {
    // Speed from ERPM: erpm / (pole_pairs * 60 / wheel_circ)
    double speed = vesc::speed_from_erpm(10000, 15, 0.8778);
    REQUIRE(speed > 0);
    REQUIRE_THAT(speed, Catch::Matchers::WithinAbs(10000.0 / (15.0 * 60.0 / 0.8778), 0.001));

    // Edge cases
    REQUIRE_THAT(vesc::speed_from_erpm(0, 15, 0.88), Catch::Matchers::WithinAbs(0.0, 0.001));
    REQUIRE_THAT(vesc::speed_from_erpm(1000, 0, 0.88), Catch::Matchers::WithinAbs(0.0, 0.001));

    // Battery percent
    REQUIRE_THAT(vesc::battery_percent(72.0, 60.0, 84.0), Catch::Matchers::WithinAbs(50.0, 0.01));
    REQUIRE_THAT(vesc::battery_percent(84.0, 60.0, 84.0), Catch::Matchers::WithinAbs(100.0, 0.01));
    REQUIRE_THAT(vesc::battery_percent(60.0, 60.0, 84.0), Catch::Matchers::WithinAbs(0.0, 0.01));
    REQUIRE_THAT(vesc::battery_percent(90.0, 60.0, 84.0), Catch::Matchers::WithinAbs(100.0, 0.01));
    REQUIRE_THAT(vesc::battery_percent(50.0, 60.0, 84.0), Catch::Matchers::WithinAbs(0.0, 0.01));
}

TEST_CASE("Storage AppData serializes and deserializes boards and profiles", "[storage]") {
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
    REQUIRE(!json.empty());

    auto loaded = nosedive::app_data_from_json(json);
    REQUIRE(loaded.boards.size() == 1u);
    REQUIRE(loaded.rider_profiles.size() == 1u);
    REQUIRE(loaded.active_profile_id == "profile-uuid-1");

    auto& lb = loaded.boards[0];
    REQUIRE(lb.id == "board-uuid-123");
    REQUIRE(lb.name == "My OneWheel");
    REQUIRE(lb.ble_name == "OW-1234");
    REQUIRE(lb.hw_name == "Little FOCer V3.1");
    REQUIRE(lb.fw_major == 6);
    REQUIRE(lb.fw_minor == 2);
    REQUIRE(lb.motor_pole_pairs == 15);
    REQUIRE_THAT(lb.wheel_circumference_m, Catch::Matchers::WithinAbs(0.92, 0.001));
    REQUIRE(lb.battery_series_cells == 20);
    REQUIRE_THAT(lb.battery_voltage_min, Catch::Matchers::WithinAbs(60.0, 0.01));
    REQUIRE_THAT(lb.battery_voltage_max, Catch::Matchers::WithinAbs(84.0, 0.01));
    REQUIRE_THAT(lb.lifetime_distance_m, Catch::Matchers::WithinAbs(12345.67, 0.01));
    REQUIRE(lb.ride_count == 42);
    REQUIRE(lb.wizard_complete);
    REQUIRE(lb.refloat_version == "1.3.0");
    REQUIRE(lb.last_connected == 1700000000);
    REQUIRE(lb.active_profile_id == "profile-uuid-1");

    auto& lp = loaded.rider_profiles[0];
    REQUIRE(lp.id == "profile-uuid-1");
    REQUIRE(lp.name == "Chill");
    REQUIRE(lp.icon == "leaf");
    REQUIRE(!lp.is_built_in);
    REQUIRE_THAT(lp.responsiveness, Catch::Matchers::WithinAbs(3.0, 0.01));
    REQUIRE_THAT(lp.stability, Catch::Matchers::WithinAbs(7.0, 0.01));
    REQUIRE_THAT(lp.footpad_sensitivity, Catch::Matchers::WithinAbs(5.0, 0.01));
    REQUIRE_THAT(lp.disengage_speed, Catch::Matchers::WithinAbs(3.5, 0.01));
}

// --- Engine FFI round-trip ---
TEST_CASE("Engine FFI saves and reloads boards and profiles", "[engine][ffi]") {
    const char* path = "/tmp/nosedive_test_engine.json";
    std::remove(path); // clean slate

    auto* e = nd_engine_create(path);
    REQUIRE(e != nullptr);

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
    REQUIRE(nd_engine_board_count(e) == 1u);

    // Save a profile via engine
    nd_rider_profile_t cp = {};
    std::strncpy(cp.id, "ffi-profile-1", sizeof(cp.id) - 1);
    std::strncpy(cp.name, "Flow", sizeof(cp.name) - 1);
    std::strncpy(cp.icon, "wind", sizeof(cp.icon) - 1);
    cp.responsiveness = 5.0;
    cp.stability = 5.0;
    nd_engine_save_profile(e, cp);
    REQUIRE(nd_engine_profile_count(e) == 1u);

    nd_engine_set_active_profile_id(e, "ffi-profile-1");
    nd_engine_destroy(e);

    // Reload from disk
    auto* e2 = nd_engine_create(path);
    REQUIRE(e2 != nullptr);
    REQUIRE(nd_engine_board_count(e2) == 1u);
    REQUIRE(nd_engine_profile_count(e2) == 1u);

    nd_board_t lb = nd_engine_get_board(e2, 0);
    REQUIRE(std::string(lb.id) == "ffi-board-1");
    REQUIRE(std::string(lb.name) == "Test Board");
    REQUIRE(lb.fw_major == 6);
    REQUIRE(lb.wizard_complete);
    REQUIRE_THAT(lb.wheel_circumference_m, Catch::Matchers::WithinAbs(0.88, 0.001));

    nd_rider_profile_t lp = nd_engine_get_profile(e2, 0);
    REQUIRE(std::string(lp.id) == "ffi-profile-1");
    REQUIRE(std::string(lp.name) == "Flow");
    REQUIRE_THAT(lp.responsiveness, Catch::Matchers::WithinAbs(5.0, 0.01));

    auto* aid = nd_engine_active_profile_id(e2);
    REQUIRE(aid != nullptr);
    REQUIRE(std::string(aid) == "ffi-profile-1");

    nd_engine_destroy(e2);
    std::remove(path);
}

// --- Engine receive_bytes + domain callbacks ---
TEST_CASE("Engine dispatches board callback on FW version response", "[engine]") {
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
    REQUIRE(!written.empty());

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
    REQUIRE(!framed.empty());

    // Feed raw bytes — engine decodes internally
    nd_engine_receive_bytes(e, framed.data(), framed.size());

    REQUIRE(got_board);
    REQUIRE(last_board.fw_major == 6);
    REQUIRE(last_board.fw_minor == 5);
    REQUIRE(std::string(last_board.hw_name) == "TestHW");
    REQUIRE(last_board.show_wizard);

    // Disconnect
    nd_engine_on_disconnected(e);

    nd_engine_destroy(e);
    std::remove(path);
}
