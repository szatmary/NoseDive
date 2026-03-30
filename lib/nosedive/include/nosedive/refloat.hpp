#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nosedive {
namespace refloat {

constexpr uint8_t kPackageMagic = 0x65;

// Command IDs from Refloat main.c
enum class CommandID : uint8_t {
    Info            = 0,
    GetRTData       = 1,
    RTTune          = 2,
    TuneDefaults    = 3,
    CfgSave         = 4,
    CfgRestore      = 5,
    TuneOther       = 6,
    RCMove          = 7,
    Booster         = 8,
    PrintInfo       = 9,
    GetAllData      = 10,
    Experiment      = 11,
    Lock            = 12,
    Handtest        = 13,
    TuneTilt        = 14,
    LightsControl   = 20,
    Flywheel        = 22,
    LCMPoll         = 24,
    LCMLightInfo    = 25,
    LCMLightCtrl    = 26,
    LCMDeviceInfo   = 27,
    ChargingState   = 28,
    LCMGetBattery   = 29,
    RealtimeData    = 31,
    RealtimeDataIDs = 32,
    AlertsList      = 35,
    AlertsControl   = 36,
    DataRecordReq   = 41,
};

enum class RunState : uint8_t {
    Disabled = 0,
    Startup  = 1,
    Ready    = 2,
    Running  = 3,
};

enum class Mode : uint8_t {
    Normal   = 0,
    Handtest = 1,
    Flywheel = 2,
};

enum class StopCondition : uint8_t {
    None        = 0,
    Pitch       = 1,
    Roll        = 2,
    SwitchHalf  = 3,
    SwitchFull  = 4,
    ReverseStop = 5,
    Quickstop   = 6,
};

enum class SAT : uint8_t {
    None          = 0,
    Centering     = 1,
    ReverseStop   = 2,
    PBSpeed       = 5,
    PBDuty        = 6,
    PBError       = 7,
    PBHighVoltage = 10,
    PBLowVoltage  = 11,
    PBTemperature = 12,
};

enum class FootpadState : uint8_t {
    None  = 0,
    Left  = 1,
    Right = 2,
    Both  = 3,
};

struct State {
    RunState run_state       = RunState::Disabled;
    Mode mode                = Mode::Normal;
    SAT sat                  = SAT::None;
    StopCondition stop       = StopCondition::None;
    FootpadState footpad     = FootpadState::None;
    bool charging            = false;
    bool wheelslip           = false;
    bool dark_ride           = false;
};

struct RTData {
    State state;

    // Motor data
    double speed         = 0;
    double erpm          = 0;
    double motor_current = 0;
    double dir_current   = 0;
    double filt_current  = 0;
    double duty_cycle    = 0;
    double batt_voltage  = 0;
    double batt_current  = 0;
    double mosfet_temp   = 0;
    double motor_temp    = 0;

    // IMU
    double pitch         = 0;
    double balance_pitch = 0;
    double roll          = 0;

    // Footpad ADC
    double adc1          = 0;
    double adc2          = 0;

    // Remote
    double remote_input  = 0;

    // Setpoints
    double setpoint            = 0;
    double atr_setpoint        = 0;
    double brake_tilt_setpoint = 0;
    double torque_tilt_setpoint = 0;
    double turn_tilt_setpoint  = 0;
    double remote_setpoint     = 0;
    double balance_current     = 0;
    double atr_accel_diff      = 0;
    double atr_speed_boost     = 0;
    double booster_current     = 0;
};

struct PackageInfo {
    std::string name;
    uint8_t major = 0;
    uint8_t minor = 0;
    uint8_t patch = 0;
    std::string suffix;
};

// --- Compat decoders for packed state byte ---
RunState decode_state_compat(uint8_t v);
StopCondition decode_stop_compat(uint8_t v);
SAT decode_sat_compat(uint8_t v);

// --- Parsers ---
std::optional<RTData> parse_all_data(const uint8_t* data, size_t len, uint8_t mode);
std::optional<RTData> parse_rt_data(const uint8_t* data, size_t len);
std::optional<PackageInfo> parse_info(const uint8_t* data, size_t len);

// --- Command builders ---
// Build a Refloat command payload (ready to wrap in COMM_CUSTOM_APP_DATA).
// Returns: [magic][cmd_id][...payload]
std::vector<uint8_t> build_command(CommandID cmd, const uint8_t* payload = nullptr, size_t len = 0);
std::vector<uint8_t> build_get_all_data(uint8_t mode);
std::vector<uint8_t> build_get_rt_data();
std::vector<uint8_t> build_info_request();

} // namespace refloat
} // namespace nosedive
