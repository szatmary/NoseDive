#include "nosedive/commands.hpp"
#include "nosedive/protocol.hpp"
#include <optional>

namespace nosedive {

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

std::optional<Values> parse_values(const uint8_t* data, size_t len) {
    if (len < 53) return std::nullopt;

    Buffer buf(std::vector<uint8_t>(data, data + len));
    Values v;
    v.temp_mosfet       = buf.read_float16(10);
    v.temp_motor        = buf.read_float16(10);
    v.avg_motor_current = buf.read_float32(100);
    v.avg_input_current = buf.read_float32(100);
    buf.read_int32(); // skip id
    buf.read_int32(); // skip iq
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

} // namespace nosedive
