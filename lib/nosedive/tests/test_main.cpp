// Minimal test runner — no dependencies
#include "nosedive/nosedive.h"
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

    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
