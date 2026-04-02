#include "vesc/refloat.hpp"
#include "vesc/protocol.hpp"
#include <algorithm>
#include <cstring>

namespace vesc {
namespace refloat {

// --- Compat decoders ---

RunState decode_state_compat(uint8_t v) {
    switch (v) {
        case 0:  return RunState::Startup;
        case 1: case 2: case 3: case 4: case 5:
                 return RunState::Running;
        case 6: case 7: case 8: case 9: case 11: case 12: case 13:
                 return RunState::Ready;
        case 14: return RunState::Ready;  // charging
        case 15: return RunState::Disabled;
        default: return RunState::Ready;
    }
}

StopCondition decode_stop_compat(uint8_t v) {
    switch (v) {
        case 6:  return StopCondition::Pitch;
        case 7:  return StopCondition::Roll;
        case 8:  return StopCondition::SwitchHalf;
        case 9:  return StopCondition::SwitchFull;
        case 12: return StopCondition::ReverseStop;
        case 13: return StopCondition::Quickstop;
        default: return StopCondition::None;
    }
}

SAT decode_sat_compat(uint8_t v) {
    switch (v) {
        case 0: return SAT::Centering;
        case 1: return SAT::ReverseStop;
        case 2: return SAT::None;
        case 3: return SAT::PBDuty;
        case 4: return SAT::PBHighVoltage;
        case 5: return SAT::PBLowVoltage;
        case 6: return SAT::PBTemperature;
        case 7: return SAT::PBSpeed;
        case 8: return SAT::PBError;
        default: return SAT::None;
    }
}

// --- Parsers ---

std::optional<RTData> parse_all_data(const uint8_t* data, size_t len, uint8_t mode) {
    if (len < 32) return std::nullopt;

    Buffer buf(std::vector<uint8_t>(data, data + len));
    RTData rt;

    // Byte 0: mode echo (69 = VESC fault)
    uint8_t resp_mode = buf.read_uint8();
    if (resp_mode == 69) return std::nullopt;

    // Bytes 1-2: balance_current (float16, scale=10)
    rt.balance_current = buf.read_float16(10.0);
    // Bytes 3-4: balance_pitch
    rt.balance_pitch = buf.read_float16(10.0);
    // Bytes 5-6: roll
    rt.roll = buf.read_float16(10.0);

    // Byte 7: (state_compat & 0xF) | (sat_compat << 4)
    uint8_t state_byte = buf.read_uint8();
    uint8_t state_compat = state_byte & 0x0F;
    uint8_t sat_compat = state_byte >> 4;
    rt.state.run_state = decode_state_compat(state_compat);
    rt.state.stop = decode_stop_compat(state_compat);
    rt.state.sat = decode_sat_compat(sat_compat);

    // Byte 8: (switch_state & 0xF) | (beep_reason << 4)
    uint8_t switch_byte = buf.read_uint8();
        { uint8_t fp = switch_byte & 0x0F; rt.state.footpad = fp <= 3 ? static_cast<FootpadState>(fp) : FootpadState::None; }

    // Byte 9-10: ADC
    rt.adc1 = static_cast<double>(buf.read_uint8()) / 50.0;
    rt.adc2 = static_cast<double>(buf.read_uint8()) / 50.0;

    // Bytes 11-16: setpoints as uint8 * 5 + 128
    rt.setpoint             = (static_cast<double>(buf.read_uint8()) - 128.0) / 5.0;
    rt.atr_setpoint         = (static_cast<double>(buf.read_uint8()) - 128.0) / 5.0;
    rt.brake_tilt_setpoint  = (static_cast<double>(buf.read_uint8()) - 128.0) / 5.0;
    rt.torque_tilt_setpoint = (static_cast<double>(buf.read_uint8()) - 128.0) / 5.0;
    rt.turn_tilt_setpoint   = (static_cast<double>(buf.read_uint8()) - 128.0) / 5.0;
    rt.remote_setpoint      = (static_cast<double>(buf.read_uint8()) - 128.0) / 5.0;

    // Bytes 17-18: pitch
    rt.pitch = buf.read_float16(10.0);

    // Byte 19: booster current + 128
    rt.booster_current = static_cast<double>(buf.read_uint8()) - 128.0;

    // Bytes 20-21: battery voltage
    rt.batt_voltage = buf.read_float16(10.0);

    // Bytes 22-23: erpm
    rt.erpm = static_cast<double>(buf.read_int16());

    // Bytes 24-25: speed m/s
    rt.speed = buf.read_float16(10.0);

    // Bytes 26-27: motor current
    rt.motor_current = buf.read_float16(10.0);

    // Bytes 28-29: battery current
    rt.batt_current = buf.read_float16(10.0);

    // Byte 30: duty * 100 + 128
    rt.duty_cycle = (static_cast<double>(buf.read_uint8()) - 128.0) / 100.0;

    // Byte 31: foc_id (skip)
    buf.read_uint8();

    // Mode >= 2: additional data
    if (mode >= 2 && buf.remaining() >= 7) {
        // float32_auto: distance (skip)
        buf.read_float32_auto();
        // mosfet_temp * 2
        rt.mosfet_temp = static_cast<double>(buf.read_uint8()) / 2.0;
        // motor_temp * 2
        rt.motor_temp = static_cast<double>(buf.read_uint8()) / 2.0;
        // reserved batt_temp
        buf.read_uint8();
    }

    return rt;
}

std::optional<RTData> parse_rt_data(const uint8_t* data, size_t len) {
    if (len < 40) return std::nullopt;

    Buffer buf(std::vector<uint8_t>(data, data + len));
    RTData rt;

    rt.balance_current = buf.read_float32_auto();
    rt.balance_pitch = buf.read_float32_auto();
    rt.roll = buf.read_float32_auto();

    // State byte
    uint8_t state_byte = buf.read_uint8();
    uint8_t state_compat = state_byte & 0x0F;
    uint8_t sat_compat = state_byte >> 4;
    rt.state.run_state = decode_state_compat(state_compat);
    rt.state.stop = decode_stop_compat(state_compat);
    rt.state.sat = decode_sat_compat(sat_compat);

    // Switch byte
    uint8_t switch_byte = buf.read_uint8();
        { uint8_t fp = switch_byte & 0x0F; rt.state.footpad = fp <= 3 ? static_cast<FootpadState>(fp) : FootpadState::None; }

    rt.adc1 = buf.read_float32_auto();
    rt.adc2 = buf.read_float32_auto();
    rt.setpoint = buf.read_float32_auto();
    rt.atr_setpoint = buf.read_float32_auto();
    rt.brake_tilt_setpoint = buf.read_float32_auto();
    rt.torque_tilt_setpoint = buf.read_float32_auto();
    rt.turn_tilt_setpoint = buf.read_float32_auto();
    rt.remote_setpoint = buf.read_float32_auto();
    rt.pitch = buf.read_float32_auto();
    rt.filt_current = buf.read_float32_auto();

    if (buf.remaining() >= 4) rt.atr_accel_diff = buf.read_float32_auto();
    if (buf.remaining() >= 4) rt.booster_current = buf.read_float32_auto();
    if (buf.remaining() >= 4) rt.dir_current = buf.read_float32_auto();
    if (buf.remaining() >= 4) rt.remote_input = buf.read_float32_auto();

    return rt;
}

std::optional<PackageInfo> parse_info(const uint8_t* data, size_t len) {
    if (len < 50) return std::nullopt;

    PackageInfo info;
    size_t idx = 2; // skip version byte + flags byte

    // Name: fixed 20 bytes
    std::string name(reinterpret_cast<const char*>(data + idx), 20);
    auto null_pos = name.find('\0');
    if (null_pos != std::string::npos) name.resize(null_pos);
    info.name = name;
    idx += 20;

    info.major = data[idx++];
    info.minor = data[idx++];
    info.patch = data[idx++];

    // Suffix: fixed 20 bytes
    std::string suffix(reinterpret_cast<const char*>(data + idx), 20);
    null_pos = suffix.find('\0');
    if (null_pos != std::string::npos) suffix.resize(null_pos);
    info.suffix = suffix;

    return info;
}

// --- Command builders ---

std::vector<uint8_t> build_command(CommandID cmd, const uint8_t* payload, size_t len) {
    std::vector<uint8_t> data;
    data.reserve(2 + len);
    data.push_back(kPackageMagic);
    data.push_back(static_cast<uint8_t>(cmd));
    if (payload && len > 0) {
        data.insert(data.end(), payload, payload + len);
    }
    return data;
}

std::vector<uint8_t> build_get_all_data(uint8_t mode) {
    return build_command(CommandID::GetAllData, &mode, 1);
}

std::vector<uint8_t> build_get_rt_data() {
    return build_command(CommandID::GetRTData);
}

std::vector<uint8_t> build_info_request() {
    uint8_t version = 2;
    return build_command(CommandID::Info, &version, 1);
}

} // namespace refloat
} // namespace vesc
