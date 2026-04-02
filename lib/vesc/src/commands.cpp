#include "vesc/commands.hpp"
#include "vesc/protocol.hpp"
#include <algorithm>
#include <cstdio>

namespace vesc {

const char* fault_code_str(FaultCode f) {
    switch (f) {
        case FaultCode::None:                   return "NONE";
        case FaultCode::OverVoltage:            return "OVER_VOLTAGE";
        case FaultCode::UnderVoltage:           return "UNDER_VOLTAGE";
        case FaultCode::DRV:                    return "DRV";
        case FaultCode::AbsOverCurrent:         return "ABS_OVER_CURRENT";
        case FaultCode::OverTempFET:            return "OVER_TEMP_FET";
        case FaultCode::OverTempMotor:          return "OVER_TEMP_MOTOR";
        case FaultCode::GateDriverOverVoltage:  return "GATE_DRIVER_OVER_VOLTAGE";
        case FaultCode::GateDriverUnderVoltage: return "GATE_DRIVER_UNDER_VOLTAGE";
        case FaultCode::MCUUnderVoltage:        return "MCU_UNDER_VOLTAGE";
        case FaultCode::BootingFromWatchdog:    return "BOOTING_FROM_WATCHDOG";
        case FaultCode::EncoderSPI:             return "ENCODER_SPI";
        default:                                return "UNKNOWN";
    }
}

// --- RefloatInfo ---

std::string RefloatInfo::version_string() const {
    char buf[64];
    if (suffix.empty()) {
        std::snprintf(buf, sizeof(buf), "%u.%u.%u", major, minor, patch);
    } else {
        std::snprintf(buf, sizeof(buf), "%u.%u.%u-%s", major, minor, patch, suffix.c_str());
    }
    return buf;
}

// --- Parsers ---

std::optional<Values> parse_values(const uint8_t* data, size_t len) {
    // Full payload including cmd byte at [0]
    if (len < 53) return std::nullopt;

    Buffer buf(std::vector<uint8_t>(data + 1, data + len)); // skip cmd byte
    Values v;
    v.temp_mosfet       = buf.read_float16(10);
    v.temp_motor        = buf.read_float16(10);
    v.avg_motor_current = buf.read_float32(100);
    v.avg_input_current = buf.read_float32(100);
    v.avg_id            = buf.read_float32(100);
    v.avg_iq            = buf.read_float32(100);
    v.duty_cycle        = buf.read_float16(1000);
    v.rpm               = buf.read_float32(1);
    v.voltage           = buf.read_float32(10);
    v.amp_hours         = buf.read_float32(10000);
    v.amp_hours_charged = buf.read_float32(10000);
    v.watt_hours        = buf.read_float32(10000);
    v.watt_hours_charged = buf.read_float32(10000);
    v.tachometer        = buf.read_int32();
    v.tachometer_abs    = buf.read_int32();
    v.fault             = static_cast<FaultCode>(buf.read_uint8());
    return v;
}

std::optional<FWVersion> parse_fw_version(const uint8_t* data, size_t len) {
    // [cmd=0][major][minor][hw_name\0][uuid:12][isPaired:1][fwTest:1][hwType:1]
    // [customConfigCount:1][hasPhaseFilters:1][qmlHW:1][qmlApp:1][nrfFlags:1][packageName\0]
    if (len < 4) return std::nullopt;
    if (data[0] != static_cast<uint8_t>(CommPacketID::FWVersion)) return std::nullopt;

    FWVersion fw;
    fw.major = data[1];
    fw.minor = data[2];

    size_t idx = 3;

    // HW name (null-terminated)
    while (idx < len && data[idx] != 0) {
        fw.hw_name += static_cast<char>(data[idx]);
        idx++;
    }
    if (idx < len) idx++; // skip null

    // UUID (12 bytes → hex string)
    if (idx + 12 <= len) {
        char hex[25];
        for (int i = 0; i < 12; i++) {
            std::snprintf(hex + i * 2, 3, "%02x", data[idx + i]);
        }
        fw.uuid = std::string(hex, 24);
        idx += 12;
    }

    // isPaired
    if (idx < len) { fw.is_paired = data[idx] != 0; idx++; }
    // FW test version (skip)
    if (idx < len) { idx++; }
    // HW type
    if (idx < len) { fw.hw_type = static_cast<HWType>(data[idx]); idx++; }
    // Custom config count
    if (idx < len) { fw.custom_config_count = data[idx]; idx++; }
    // hasPhaseFilters (skip)
    if (idx < len) { idx++; }
    // QML HW (skip)
    if (idx < len) { idx++; }
    // QML App (skip)
    if (idx < len) { idx++; }
    // NRF flags (skip)
    if (idx < len) { idx++; }
    // Package name (null-terminated)
    while (idx < len && data[idx] != 0) {
        fw.package_name += static_cast<char>(data[idx]);
        idx++;
    }

    return fw;
}

std::vector<uint8_t> parse_ping_can(const uint8_t* data, size_t len) {
    if (len < 2 || data[0] != static_cast<uint8_t>(CommPacketID::PingCAN)) return {};
    return std::vector<uint8_t>(data + 1, data + len);
}

std::optional<RefloatInfo> parse_refloat_info(const uint8_t* data, size_t len) {
    // [cmd=36][magic=0x65][cmdId=0][version][major][minor][patch][name:20][suffix:20]...
    constexpr uint8_t refloat_magic = 0x65;
    if (len < 6) return std::nullopt;
    if (data[0] != static_cast<uint8_t>(CommPacketID::CustomAppData)) return std::nullopt;
    if (data[1] != refloat_magic) return std::nullopt;
    if (data[2] != 0x00) return std::nullopt; // CommandInfo

    RefloatInfo info;
    uint8_t version = data[3];
    if (version >= 2) {
        if (len < 7 + 40) return std::nullopt; // need major+minor+patch+name(20)+suffix(20)
        info.major = data[4];
        info.minor = data[5];
        info.patch = data[6];

        // Name: 20 bytes, null-terminated
        auto read_fixed_str = [](const uint8_t* p, size_t max_len) {
            std::string s;
            for (size_t i = 0; i < max_len && p[i] != 0; i++) {
                s += static_cast<char>(p[i]);
            }
            return s;
        };

        info.name = read_fixed_str(data + 7, 20);
        info.suffix = read_fixed_str(data + 27, 20);
    }

    return info;
}

// --- Command builders ---

std::vector<uint8_t> build_command(CommPacketID cmd) {
    return { static_cast<uint8_t>(cmd) };
}

std::vector<uint8_t> build_can_forward(uint8_t target_id, const uint8_t* inner, size_t inner_len) {
    std::vector<uint8_t> payload;
    payload.reserve(2 + inner_len);
    payload.push_back(static_cast<uint8_t>(CommPacketID::ForwardCAN));
    payload.push_back(target_id);
    payload.insert(payload.end(), inner, inner + inner_len);
    return payload;
}

std::vector<uint8_t> build_fw_version_request_can(uint8_t target_id) {
    uint8_t inner = static_cast<uint8_t>(CommPacketID::FWVersion);
    return build_can_forward(target_id, &inner, 1);
}

std::vector<uint8_t> build_refloat_info_request() {
    constexpr uint8_t refloat_magic = 0x65;
    return {
        static_cast<uint8_t>(CommPacketID::CustomAppData),
        refloat_magic,
        0x00  // CommandInfo
    };
}

// --- Computed values ---

double speed_from_erpm(double erpm, int pole_pairs, double wheel_circumference_m) {
    if (pole_pairs <= 0 || wheel_circumference_m <= 0) return 0;
    double erpm_per_mps = static_cast<double>(pole_pairs) * 60.0 / wheel_circumference_m;
    return erpm / erpm_per_mps;
}

double battery_percent(double voltage, double voltage_min, double voltage_max) {
    if (voltage_max <= voltage_min) return 0;
    double pct = (voltage - voltage_min) / (voltage_max - voltage_min) * 100.0;
    return std::clamp(pct, 0.0, 100.0);
}

} // namespace vesc
