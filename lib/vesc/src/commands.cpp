#include "vesc/commands.hpp"
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

// ============================================================
// Request encoders
// ============================================================

std::vector<uint8_t> FWVersion::Request::encode() const {
    return {static_cast<uint8_t>(CommPacketID::FWVersion)};
}

std::vector<uint8_t> GetValues::Request::encode() const {
    return {static_cast<uint8_t>(CommPacketID::GetValues)};
}

std::vector<uint8_t> PingCAN::Request::encode() const {
    return {static_cast<uint8_t>(CommPacketID::PingCAN)};
}

std::vector<uint8_t> ForwardCAN::Request::encode() const {
    std::vector<uint8_t> payload;
    payload.reserve(2 + inner_payload.size());
    payload.push_back(static_cast<uint8_t>(CommPacketID::ForwardCAN));
    payload.push_back(target_id);
    payload.insert(payload.end(), inner_payload.begin(), inner_payload.end());
    return payload;
}

std::vector<uint8_t> GetIMUData::Request::encode() const {
    return {
        static_cast<uint8_t>(CommPacketID::GetIMUData),
        static_cast<uint8_t>(mask >> 8),
        static_cast<uint8_t>(mask & 0xFF)
    };
}

std::vector<uint8_t> GetValuesSetup::Request::encode() const {
    return {static_cast<uint8_t>(CommPacketID::GetValuesSetup)};
}

std::vector<uint8_t> Alive::Request::encode() const {
    return {static_cast<uint8_t>(CommPacketID::Alive)};
}

std::vector<uint8_t> GetBatteryCut::Request::encode() const {
    return {static_cast<uint8_t>(CommPacketID::GetBatteryCut)};
}

std::vector<uint8_t> DetectApplyAllFOC::Request::encode() const {
    Buffer buf;
    buf.append_uint8(static_cast<uint8_t>(CommPacketID::DetectApplyAllFOC));
    buf.append_uint8(detect_can ? 1 : 0);
    buf.append_float32(max_power_loss, 1e3);
    buf.append_float32(min_current_in, 1e3);
    buf.append_float32(max_current_in, 1e3);
    buf.append_float32(openloop_rpm, 1e3);
    buf.append_float32(sl_erpm, 1e3);
    return buf.take();
}

std::vector<uint8_t> GetStats::Request::encode() const {
    return {
        static_cast<uint8_t>(CommPacketID::GetStats),
        static_cast<uint8_t>(mask >> 8),
        static_cast<uint8_t>(mask & 0xFF)
    };
}

std::vector<uint8_t> CustomAppData::Request::encode() const {
    std::vector<uint8_t> result;
    result.reserve(1 + payload.size());
    result.push_back(static_cast<uint8_t>(CommPacketID::CustomAppData));
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

std::vector<uint8_t> GetCustomConfigXML::Request::encode() const {
    Buffer buf;
    buf.append_uint8(static_cast<uint8_t>(CommPacketID::GetCustomConfigXML));
    buf.append_uint8(config_index);
    buf.append_int32(request_len);
    buf.append_int32(offset);
    return buf.take();
}

std::vector<uint8_t> GetQMLUIApp::Request::encode() const {
    Buffer buf;
    buf.append_uint8(static_cast<uint8_t>(CommPacketID::GetQMLUIApp));
    buf.append_int32(request_len);
    buf.append_int32(offset);
    return buf.take();
}

// ============================================================
// Response decoders
// ============================================================

std::optional<FWVersion::Response> FWVersion::Response::decode(const uint8_t* data, size_t len) {
    if (len < 4) return std::nullopt;
    if (data[0] != static_cast<uint8_t>(CommPacketID::FWVersion)) return std::nullopt;

    Response fw;
    fw.major = data[1];
    fw.minor = data[2];

    size_t idx = 3;
    while (idx < len && data[idx] != 0) { fw.hw_name += static_cast<char>(data[idx]); idx++; }
    if (idx < len) idx++;

    if (idx + 12 <= len) {
        char hex[25];
        for (int i = 0; i < 12; i++) std::snprintf(hex + i * 2, 3, "%02x", data[idx + i]);
        fw.uuid = std::string(hex, 24);
        idx += 12;
    }

    if (idx < len) { fw.is_paired = data[idx] != 0; idx++; }
    if (idx < len) { idx++; } // fw test version
    if (idx < len) { fw.hw_type = static_cast<HWType>(data[idx]); idx++; }
    if (idx < len) { fw.custom_config_count = data[idx]; idx++; }
    if (idx < len) { idx++; } // hasPhaseFilters
    if (idx < len) { idx++; } // QML HW
    if (idx < len) { idx++; } // QML App
    if (idx < len) { idx++; } // NRF flags
    while (idx < len && data[idx] != 0) { fw.package_name += static_cast<char>(data[idx]); idx++; }

    return fw;
}

std::optional<GetValues::Response> GetValues::Response::decode(const uint8_t* data, size_t len) {
    if (len < 53) return std::nullopt;
    Buffer buf(std::vector<uint8_t>(data + 1, data + len));
    Response v;
    v.temp_mosfet       = buf.read_float16(10);
    v.temp_motor        = buf.read_float16(10);
    v.avg_motor_current = buf.read_float32(100);
    v.avg_input_current = buf.read_float32(100);
    v.avg_id            = buf.read_float32(100);
    v.avg_iq            = buf.read_float32(100);
    v.duty_cycle        = buf.read_float16(1000);
    v.rpm               = buf.read_float32(1);
    v.voltage           = buf.read_float16(10);
    v.amp_hours         = buf.read_float32(10000);
    v.amp_hours_charged = buf.read_float32(10000);
    v.watt_hours        = buf.read_float32(10000);
    v.watt_hours_charged = buf.read_float32(10000);
    v.tachometer        = buf.read_int32();
    v.tachometer_abs    = buf.read_int32();
    v.fault             = static_cast<FaultCode>(buf.read_uint8());
    return v;
}

std::optional<PingCAN::Response> PingCAN::Response::decode(const uint8_t* data, size_t len) {
    if (len < 1 || data[0] != static_cast<uint8_t>(CommPacketID::PingCAN)) return std::nullopt;
    Response r;
    r.device_ids.assign(data + 1, data + len);
    return r;
}

std::optional<GetIMUData::Response> GetIMUData::Response::decode(const uint8_t* data, size_t len) {
    if (len < 3) return std::nullopt;
    Buffer buf(std::vector<uint8_t>(data + 1, data + len));
    Response r;
    r.mask = buf.read_uint16();

    if (r.mask & (1 << 0)) {
        r.roll  = buf.read_float32(1e6);
        r.pitch = buf.read_float32(1e6);
        r.yaw   = buf.read_float32(1e6);
    }
    if (r.mask & (1 << 1)) {
        r.accel_x = buf.read_float32(1e6);
        r.accel_y = buf.read_float32(1e6);
        r.accel_z = buf.read_float32(1e6);
    }
    if (r.mask & (1 << 2)) {
        r.gyro_x = buf.read_float32(1e6);
        r.gyro_y = buf.read_float32(1e6);
        r.gyro_z = buf.read_float32(1e6);
    }
    if (r.mask & (1 << 3)) {
        r.mag_x = buf.read_float32(1e6);
        r.mag_y = buf.read_float32(1e6);
        r.mag_z = buf.read_float32(1e6);
    }
    if (r.mask & (1 << 4)) {
        r.qw = buf.read_float32(1e6);
        r.qx = buf.read_float32(1e6);
        r.qy = buf.read_float32(1e6);
        r.qz = buf.read_float32(1e6);
    }
    return r;
}

std::optional<GetValuesSetup::Response> GetValuesSetup::Response::decode(const uint8_t* data, size_t len) {
    if (len < 60) return std::nullopt;
    Buffer buf(std::vector<uint8_t>(data + 1, data + len));
    Response r;
    r.temp_mosfet       = buf.read_float16(10);
    r.temp_motor        = buf.read_float16(10);
    r.current_tot       = buf.read_float32(100);
    r.current_in_tot    = buf.read_float32(100);
    r.duty_cycle        = buf.read_float16(1000);
    r.rpm               = buf.read_float32(1);
    r.speed             = buf.read_float32(1000);
    r.voltage           = buf.read_float16(10);
    r.battery_level     = buf.read_float16(1000);
    r.amp_hours         = buf.read_float32(10000);
    r.amp_hours_charged = buf.read_float32(10000);
    r.watt_hours        = buf.read_float32(10000);
    r.watt_hours_charged = buf.read_float32(10000);
    r.distance          = buf.read_float32(1000);
    r.distance_abs      = buf.read_float32(1000);
    r.pid_pos           = buf.read_float32(1e6);
    r.fault             = static_cast<FaultCode>(buf.read_uint8());
    r.controller_id     = buf.read_uint8();
    r.num_vescs         = buf.read_uint8();
    r.wh_batt_left      = buf.read_float32(1000);
    r.odometer          = buf.read_uint32();
    r.uptime_ms         = buf.read_uint32();
    return r;
}

std::optional<GetBatteryCut::Response> GetBatteryCut::Response::decode(const uint8_t* data, size_t len) {
    if (len < 9) return std::nullopt;
    Buffer buf(std::vector<uint8_t>(data + 1, data + len));
    Response r;
    r.voltage_start = buf.read_float32(1000);
    r.voltage_end   = buf.read_float32(1000);
    return r;
}

std::optional<DetectApplyAllFOC::Response> DetectApplyAllFOC::Response::decode(const uint8_t* data, size_t len) {
    if (len < 3) return std::nullopt;
    Buffer buf(std::vector<uint8_t>(data + 1, data + len));
    Response r;
    r.result = buf.read_int16();
    return r;
}

std::optional<GetStats::Response> GetStats::Response::decode(const uint8_t* data, size_t len) {
    if (len < 5) return std::nullopt;
    Buffer buf(std::vector<uint8_t>(data + 1, data + len));
    Response r;
    r.mask = buf.read_uint32();

    if (r.mask & (1 << 0)) r.speed_avg = buf.read_float32_auto();
    if (r.mask & (1 << 1)) r.speed_max = buf.read_float32_auto();
    if (r.mask & (1 << 2)) r.power_avg = buf.read_float32_auto();
    if (r.mask & (1 << 3)) r.power_max = buf.read_float32_auto();
    if (r.mask & (1 << 4)) r.current_avg = buf.read_float32_auto();
    if (r.mask & (1 << 5)) r.current_max = buf.read_float32_auto();
    if (r.mask & (1 << 6)) r.temp_mos_avg = buf.read_float32_auto();
    if (r.mask & (1 << 7)) r.temp_mos_max = buf.read_float32_auto();
    if (r.mask & (1 << 8)) r.temp_motor_avg = buf.read_float32_auto();
    if (r.mask & (1 << 9)) r.temp_motor_max = buf.read_float32_auto();
    if (r.mask & (1 << 10)) r.count_time = buf.read_float32_auto();
    return r;
}

std::optional<GetCustomConfigXML::Response> GetCustomConfigXML::Response::decode(const uint8_t* data_ptr, size_t len) {
    if (len < 10) return std::nullopt;
    Buffer buf(std::vector<uint8_t>(data_ptr + 1, data_ptr + len));
    Response r;
    r.config_index = buf.read_uint8();
    r.total_size   = buf.read_int32();
    r.offset       = buf.read_int32();
    size_t remaining = len - 10;
    if (remaining > 0) {
        r.data.assign(data_ptr + 10, data_ptr + len);
    }
    return r;
}

std::optional<GetQMLUIApp::Response> GetQMLUIApp::Response::decode(const uint8_t* data_ptr, size_t len) {
    if (len < 9) return std::nullopt;
    Buffer buf(std::vector<uint8_t>(data_ptr + 1, data_ptr + len));
    Response r;
    r.total_size = buf.read_int32();
    r.offset     = buf.read_int32();
    size_t remaining = len - 9;
    if (remaining > 0) {
        r.data.assign(data_ptr + 9, data_ptr + len);
    }
    return r;
}

// ============================================================
// Refloat helpers (kept as free functions for backward compat)
// ============================================================

std::optional<RefloatInfo> parse_refloat_info(const uint8_t* data, size_t len) {
    constexpr uint8_t refloat_magic = 0x65;
    if (len < 6) return std::nullopt;
    if (data[0] != static_cast<uint8_t>(CommPacketID::CustomAppData)) return std::nullopt;
    if (data[1] != refloat_magic) return std::nullopt;
    if (data[2] != 0x00) return std::nullopt;

    RefloatInfo info;
    uint8_t version = data[3];
    if (version >= 2) {
        if (len < 7 + 40) return std::nullopt;
        // Version 2: [version:1][flags:1][name:20][major:1][minor:1][patch:1][suffix:20]...
        size_t idx = 4;
        idx++; // flags
        auto read_fixed = [](const uint8_t* p, size_t max_len) {
            std::string s;
            for (size_t i = 0; i < max_len && p[i] != 0; i++) s += static_cast<char>(p[i]);
            return s;
        };
        info.name = read_fixed(data + idx, 20); idx += 20;
        info.major = data[idx++];
        info.minor = data[idx++];
        info.patch = data[idx++];
        info.suffix = read_fixed(data + idx, 20);
    }
    return info;
}

std::vector<uint8_t> build_refloat_info_request() {
    constexpr uint8_t refloat_magic = 0x65;
    return {
        static_cast<uint8_t>(CommPacketID::CustomAppData),
        refloat_magic,
        0x00,  // CommandInfo
        0x02   // version 2 request
    };
}

// ============================================================
// Utility
// ============================================================

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
