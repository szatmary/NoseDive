#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nosedive {

// VESC command identifiers from datatypes.h
enum class CommPacketID : uint8_t {
    FWVersion                  = 0,
    JumpToBootloader           = 1,
    EraseNewApp                = 2,
    WriteNewAppData            = 3,
    GetValues                  = 4,
    SetDuty                    = 5,
    SetCurrent                 = 6,
    SetCurrentBrake            = 7,
    SetRPM                     = 8,
    SetPos                     = 9,
    SetHandbrake               = 10,
    SetDetect                  = 11,
    SetServoPos                = 12,
    SetMCConf                  = 13,
    GetMCConf                  = 14,
    GetMCConfDefault           = 15,
    SetAppConf                 = 16,
    GetAppConf                 = 17,
    GetAppConfDefault          = 18,
    SamplePrint                = 19,
    TerminalCmd                = 20,
    PrintText                  = 21,
    Rotor                      = 22,
    Experiment                 = 23,
    DetectMotorParam           = 24,
    DetectMotorRL              = 25,
    DetectMotorFlux            = 26,
    DetectEncoder              = 27,
    DetectHallFOC              = 28,
    Reboot                     = 29,
    Alive                      = 30,
    GetDecodedPPM              = 31,
    GetDecodedADC              = 32,
    GetDecodedChuk             = 33,
    ForwardCAN                 = 34,
    SetChuckData               = 35,
    CustomAppData              = 36,
    NRFStartPairing            = 37,
    GetValuesSetup             = 47,
    SetMCConfTemp              = 48,
    SetMCConfTempSetup         = 49,
    GetValuesSelective         = 50,
    GetValuesSetupSelective    = 51,
    DetectMotorFluxOpenloop    = 57,
    DetectApplyAllFOC          = 58,
    PingCAN                    = 62,
    AppDisableOutput           = 63,
    TerminalCmdSync            = 64,
    GetIMUData                 = 65,
    GetDecodedBalance          = 79,
    SetCurrentRel              = 84,
    CANFwdFrame                = 85,
    SetBatteryCut              = 86,
    SetBLEName                 = 87,
    SetBLEPin                  = 88,
    SetCANMode                 = 89,
    GetIMUCalibration          = 90,
    GetMCConfTemp              = 91,
    GetCustomConfigXML         = 92,
    GetCustomConfig            = 93,
    GetCustomConfigDefault     = 94,
    SetCustomConfig            = 95,
    BMSGetValues               = 96,
    SetOdometer                = 110,
    GetBatteryCut              = 115,
    GetQMLUIHW                 = 117,
    GetQMLUIApp                = 118,
    CustomHWData               = 119,
    GetStats                   = 128,
    ResetStats                 = 129,
    GetGNSS                    = 150,
    Shutdown                   = 156,
    FWInfo                     = 157,
    MotorEStop                 = 159,
};

// Hardware type from VESC datatypes.h
enum class HWType : uint8_t {
    VESC        = 0,
    VESCExpress = 3,
};

// Fault codes from VESC datatypes.h
enum class FaultCode : uint8_t {
    None                  = 0,
    OverVoltage           = 1,
    UnderVoltage          = 2,
    DRV                   = 3,
    AbsOverCurrent        = 4,
    OverTempFET           = 5,
    OverTempMotor         = 6,
    GateDriverOverVoltage = 7,
    GateDriverUnderVoltage = 8,
    MCUUnderVoltage       = 9,
    BootingFromWatchdog   = 10,
    EncoderSPI            = 11,
};

const char* fault_code_str(FaultCode f);

// Telemetry values from COMM_GET_VALUES
struct Values {
    double temp_mosfet      = 0;
    double temp_motor       = 0;
    double avg_motor_current = 0;
    double avg_input_current = 0;
    double avg_id           = 0;
    double avg_iq           = 0;
    double duty_cycle       = 0;
    double rpm              = 0;
    double voltage          = 0;
    double amp_hours        = 0;
    double amp_hours_charged = 0;
    double watt_hours       = 0;
    double watt_hours_charged = 0;
    int32_t tachometer      = 0;
    int32_t tachometer_abs  = 0;
    FaultCode fault         = FaultCode::None;
    double pid_pos          = 0;
    uint8_t controller_id   = 0;
    uint8_t status          = 0;
};

// Firmware version info
struct FWVersion {
    uint8_t major = 0;
    uint8_t minor = 0;
    std::string hw_name;
    std::vector<uint8_t> uuid;
};

// Parse a COMM_GET_VALUES response payload (after command byte).
// Returns nullopt if the data is too short.
std::optional<Values> parse_values(const uint8_t* data, size_t len);

} // namespace nosedive
