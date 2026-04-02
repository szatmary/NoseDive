#pragma once

#include "vesc/protocol.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vesc {

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
    QmluiErase                 = 120,
    QmluiWrite                 = 121,
    GetStats                   = 128,
    ResetStats                 = 129,
    LispWriteCode              = 131,
    LispEraseCode              = 132,
    LispSetRunning             = 133,
    LispGetStats               = 134,
    LispReplCmd                = 138,
    LispStreamCode             = 139,
    GetGNSS                    = 150,
    Shutdown                   = 156,
    FWInfo                     = 157,
    MotorEStop                 = 159,
};

// Hardware type
enum class HWType : uint8_t {
    VESC        = 0,
    VESCExpress = 3,
};

// Fault codes
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

// ============================================================
// Command / Response types
// Convention: each struct has:
//   static constexpr CommPacketID id
//   struct Request  { std::vector<uint8_t> encode() const; }
//   struct Response { static std::optional<Response> decode(const uint8_t* data, size_t len); }
// ============================================================

// --- COMM_FW_VERSION (0x00) ---
struct FWVersion {
    static constexpr CommPacketID id = CommPacketID::FWVersion;

    struct Request {
        std::vector<uint8_t> encode() const;
    };

    struct Response {
        uint8_t major = 0;
        uint8_t minor = 0;
        std::string hw_name;
        std::string uuid;
        HWType hw_type = HWType::VESC;
        uint8_t custom_config_count = 0;
        std::string package_name;
        bool is_paired = false;

        static std::optional<Response> decode(const uint8_t* data, size_t len);
    };
};

// --- COMM_GET_VALUES (0x04) ---
struct GetValues {
    static constexpr CommPacketID id = CommPacketID::GetValues;

    struct Request {
        std::vector<uint8_t> encode() const;
    };

    struct Response {
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

        static std::optional<Response> decode(const uint8_t* data, size_t len);
    };
};

// --- COMM_PING_CAN (0x3E) ---
struct PingCAN {
    static constexpr CommPacketID id = CommPacketID::PingCAN;

    struct Request {
        std::vector<uint8_t> encode() const;
    };

    struct Response {
        std::vector<uint8_t> device_ids;

        static std::optional<Response> decode(const uint8_t* data, size_t len);
    };
};

// --- COMM_FORWARD_CAN (0x22) ---
struct ForwardCAN {
    static constexpr CommPacketID id = CommPacketID::ForwardCAN;

    struct Request {
        uint8_t target_id = 0;
        std::vector<uint8_t> inner_payload;

        std::vector<uint8_t> encode() const;
    };
    // Response is the inner command's response (no wrapping)
};

// --- COMM_GET_IMU_DATA (0x41) ---
struct GetIMUData {
    static constexpr CommPacketID id = CommPacketID::GetIMUData;

    struct Request {
        uint16_t mask = 0xFFFF;
        std::vector<uint8_t> encode() const;
    };

    struct Response {
        uint16_t mask = 0;
        double roll = 0, pitch = 0, yaw = 0;           // degrees
        double accel_x = 0, accel_y = 0, accel_z = 0;  // m/s²
        double gyro_x = 0, gyro_y = 0, gyro_z = 0;     // deg/s
        double mag_x = 0, mag_y = 0, mag_z = 0;        // Gauss
        double qw = 1, qx = 0, qy = 0, qz = 0;        // quaternion

        static std::optional<Response> decode(const uint8_t* data, size_t len);
    };
};

// --- COMM_GET_VALUES_SETUP (0x2F) ---
struct GetValuesSetup {
    static constexpr CommPacketID id = CommPacketID::GetValuesSetup;

    struct Request {
        std::vector<uint8_t> encode() const;
    };

    struct Response {
        double temp_mosfet = 0;
        double temp_motor = 0;
        double current_tot = 0;
        double current_in_tot = 0;
        double duty_cycle = 0;
        double rpm = 0;
        double speed = 0;
        double voltage = 0;
        double battery_level = 0;     // 0-1
        double amp_hours = 0;
        double amp_hours_charged = 0;
        double watt_hours = 0;
        double watt_hours_charged = 0;
        double distance = 0;          // meters
        double distance_abs = 0;
        double pid_pos = 0;
        FaultCode fault = FaultCode::None;
        uint8_t controller_id = 0;
        uint8_t num_vescs = 0;
        double wh_batt_left = 0;
        uint32_t odometer = 0;        // meters
        uint32_t uptime_ms = 0;

        static std::optional<Response> decode(const uint8_t* data, size_t len);
    };
};

// --- COMM_ALIVE (0x1E) ---
struct Alive {
    static constexpr CommPacketID id = CommPacketID::Alive;

    struct Request {
        std::vector<uint8_t> encode() const;
    };
    // No response
};

// --- COMM_GET_BATTERY_CUT (0x73) ---
struct GetBatteryCut {
    static constexpr CommPacketID id = CommPacketID::GetBatteryCut;

    struct Request {
        std::vector<uint8_t> encode() const;
    };

    struct Response {
        double voltage_start = 0;
        double voltage_end = 0;

        static std::optional<Response> decode(const uint8_t* data, size_t len);
    };
};

// --- COMM_DETECT_APPLY_ALL_FOC (0x3A) ---
struct DetectApplyAllFOC {
    static constexpr CommPacketID id = CommPacketID::DetectApplyAllFOC;

    struct Request {
        bool detect_can = false;
        double max_power_loss = 1.0;
        double min_current_in = 0;
        double max_current_in = 0;
        double openloop_rpm = 1000;
        double sl_erpm = 4000;

        std::vector<uint8_t> encode() const;
    };

    struct Response {
        int16_t result = 0;  // 0 = success, negative = error

        static std::optional<Response> decode(const uint8_t* data, size_t len);
    };
};

// --- COMM_GET_STATS (0x80) ---
struct GetStats {
    static constexpr CommPacketID id = CommPacketID::GetStats;

    struct Request {
        uint16_t mask = 0x07FF;
        std::vector<uint8_t> encode() const;
    };

    struct Response {
        uint32_t mask = 0;
        double speed_avg = 0, speed_max = 0;
        double power_avg = 0, power_max = 0;
        double current_avg = 0, current_max = 0;
        double temp_mos_avg = 0, temp_mos_max = 0;
        double temp_motor_avg = 0, temp_motor_max = 0;
        double count_time = 0;

        static std::optional<Response> decode(const uint8_t* data, size_t len);
    };
};

// --- COMM_CUSTOM_APP_DATA (0x24) — Refloat ---
struct CustomAppData {
    static constexpr CommPacketID id = CommPacketID::CustomAppData;

    struct Request {
        std::vector<uint8_t> payload;  // magic + command + data
        std::vector<uint8_t> encode() const;
    };

    // Response is app-specific (Refloat parses its own format)
};

// --- COMM_GET_CUSTOM_CONFIG_XML (0x5C) ---
struct GetCustomConfigXML {
    static constexpr CommPacketID id = CommPacketID::GetCustomConfigXML;

    struct Request {
        uint8_t config_index = 0;
        int32_t request_len = 400;
        int32_t offset = 0;

        std::vector<uint8_t> encode() const;
    };

    struct Response {
        uint8_t config_index = 0;
        int32_t total_size = 0;
        int32_t offset = 0;
        std::vector<uint8_t> data;

        static std::optional<Response> decode(const uint8_t* data, size_t len);
    };
};

// --- COMM_GET_QML_UI_APP (0x76) ---
struct GetQMLUIApp {
    static constexpr CommPacketID id = CommPacketID::GetQMLUIApp;

    struct Request {
        int32_t request_len = 400;
        int32_t offset = 0;

        std::vector<uint8_t> encode() const;
    };

    struct Response {
        int32_t total_size = 0;
        int32_t offset = 0;
        std::vector<uint8_t> data;

        static std::optional<Response> decode(const uint8_t* data_ptr, size_t len);
    };
};

// --- Refloat info (parsed from CustomAppData) ---
struct RefloatInfo {
    std::string name;
    uint8_t major = 0;
    uint8_t minor = 0;
    uint8_t patch = 0;
    std::string suffix;

    std::string version_string() const;
};

std::optional<RefloatInfo> parse_refloat_info(const uint8_t* data, size_t len);
std::vector<uint8_t> build_refloat_info_request();

// --- Telemetry (computed from GetValues::Response + board config) ---
// This is app-level, but kept here as a convenience struct
struct Telemetry {
    double temp_mosfet      = 0;
    double temp_motor       = 0;
    double motor_current    = 0;
    double battery_current  = 0;
    double duty_cycle       = 0;
    double erpm             = 0;
    double battery_voltage  = 0;
    double amp_hours        = 0;
    double amp_hours_charged = 0;
    double watt_hours       = 0;
    double watt_hours_charged = 0;
    int32_t tachometer      = 0;
    int32_t tachometer_abs  = 0;
    FaultCode fault         = FaultCode::None;
    double speed            = 0;
    double battery_percent  = 0;
    double power            = 0;
};

// --- Utility ---
double speed_from_erpm(double erpm, int pole_pairs, double wheel_circumference_m);
double battery_percent(double voltage, double voltage_min, double voltage_max);

} // namespace vesc
