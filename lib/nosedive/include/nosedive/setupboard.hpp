#pragma once

// Board setup wizard state machine.
//
// Drives the full new-board setup process:
//   1. Check VESC Express firmware
//   2. Check BMS firmware
//   3. Check VESC motor controller firmware
//   4. Install Refloat package
//   5. Detect battery configuration
//   6. Detect footpad sensors
//   7. Calibrate IMU (gyro/accel)
//   8. Detect motor parameters (R/L/flux)
//   9. Configure wheel diameter
//
// Each step has a phase: Working (actively doing something),
// Prompt (waiting for user input), or WaitReconnect (board rebooting).
// The UI uses step+phase to decide what to render.

#include <vesc/commands.hpp>
#include <vesc/vescpkg.hpp>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace nosedive {

enum class SetupStep : uint8_t {
    Idle,
    FWExpress,          // VESC Express (BLE bridge) firmware
    FWBMS,              // BMS firmware
    FWVESC,             // VESC motor controller firmware
    FactoryReset,       // Reset MC and App config to defaults
    InstallRefloat,     // Install/update Refloat package
    DetectFootpads,     // Check footpad ADC sensors
    CalibrateIMU,       // Calibrate gyroscope and accelerometer
    DetectMotor,        // Run motor detection (R/L/flux linkage)
    ConfigureWheel,     // Set wheel diameter
    ConfigurePower,     // Set battery cell count and voltage cutoffs
    Done,
};

// Where within a step the wizard is
enum class StepPhase : uint8_t {
    Working,            // Actively checking/uploading/detecting
    Prompt,             // Waiting for user decision (update/skip/install)
    WaitReconnect,      // Board rebooting after firmware update
};

// Which device a firmware step targets (derived from step)
enum class UpdateTarget : uint8_t {
    None,
    Express,
    BMS,
    VESC,
};

// Known latest firmware versions (bundled with app)
struct LatestFW {
    static constexpr uint8_t vesc_major = 6;
    static constexpr uint8_t vesc_minor = 6;
    static constexpr uint8_t express_major = 6;
    static constexpr uint8_t express_minor = 6;

    static bool is_outdated(uint8_t major, uint8_t minor,
                            uint8_t latest_major, uint8_t latest_minor) {
        if (major < latest_major) return true;
        if (major > latest_major) return false;
        return minor < latest_minor;
    }
};

// Known latest Refloat version (bundled with app)
struct LatestRefloat {
    static constexpr uint8_t major = 1;
    static constexpr uint8_t minor = 2;
    static constexpr uint8_t patch = 1;

    static bool is_outdated(uint8_t maj, uint8_t min, uint8_t pat) {
        if (maj != major) return maj < major;
        if (min != minor) return min < minor;
        return pat < patch;
    }
};

struct SetupState {
    SetupStep step = SetupStep::Idle;
    StepPhase phase = StepPhase::Working;
    std::string error;    // empty = no error
    std::string detail;   // what's happening / what was detected
};

/// Callback: wizard state changed, UI should update.
using SetupCallback = std::function<void(const SetupState&)>;

/// Callback: wizard wants to send a VESC payload.
using SetupSendCallback = std::function<void(const std::vector<uint8_t>& payload)>;

// Factory reset sub-phases (within FactoryReset step)
enum class ResetPhase : uint8_t {
    GetMCDefault,   // Waiting for GetMCConfDefault response
    SetMC,          // Waiting for SetMCConf ack
    GetAppDefault,  // Waiting for GetAppConfDefault response
    SetApp,         // Waiting for SetAppConf ack
    Done,
};

// Refloat install sub-phases (within InstallRefloat step)
enum class InstallPhase : uint8_t {
    LispErase,
    LispWrite,
    QmlErase,
    QmlWrite,
    SetRunning,
    Done,
};

/// Board setup wizard. Created by the engine when a new board is detected.
class SetupBoard {
public:
    void set_state_callback(SetupCallback cb) { state_cb_ = std::move(cb); }
    void set_send_callback(SetupSendCallback cb) { send_cb_ = std::move(cb); }

    /// Start the wizard from the beginning.
    void start();

    /// Retry the current failed step.
    void retry();

    /// Skip the current step and advance.
    void skip();

    /// Start firmware update or Refloat install for the current step.
    /// Only valid when phase == Prompt.
    void update();

    /// Notify the wizard that the board has reconnected (after firmware update).
    /// Called by the engine when on_connected fires during WaitReconnect.
    void on_reconnected();

    /// Abort the wizard entirely.
    void abort();

    /// Feed a VESC response payload. The wizard checks if it's relevant
    /// to the current step and advances if so.
    void handle_response(const uint8_t* data, size_t len);

    /// Current state (for reading without callback).
    const SetupState& state() const { return state_; }

    /// Whether the wizard is actively running (not Idle or Done).
    bool is_running() const;

    // --- Context from engine (set before start) ---

    /// CAN device IDs discovered during connection.
    std::vector<uint8_t> can_device_ids;

    /// Main VESC firmware info (if already known from connection).
    std::optional<vesc::FWVersion::Response> main_fw;

    /// Refloat version info (nullopt if not installed).
    std::optional<vesc::RefloatInfo> refloat_info;

    /// Refloat package data (set by engine before start).
    const vesc::VescPackage* refloat_package = nullptr;

    /// Chunk size for uploading Lisp/QML data.
    static constexpr size_t kUploadChunkSize = 400;

private:
    SetupState state_;
    SetupCallback state_cb_;
    SetupSendCallback send_cb_;

    void set_state(SetupStep step, StepPhase phase, const std::string& detail = "");
    void set_error(const std::string& error);
    void advance();
    void fire_callback();

    // Track which CAN devices we've checked
    bool express_checked_ = false;
    bool bms_checked_ = false;

    // Cached firmware versions from CAN devices
    std::optional<vesc::FWVersion::Response> express_fw_;
    std::optional<vesc::FWVersion::Response> bms_fw_;

    // Factory reset sub-state
    ResetPhase reset_phase_ = ResetPhase::GetMCDefault;
    std::vector<uint8_t> reset_conf_blob_;

    // Refloat install sub-state
    InstallPhase install_phase_ = InstallPhase::LispErase;
    size_t install_offset_ = 0;

    // Per-target helpers
    std::optional<uint8_t> find_can_id(UpdateTarget target) const;
    const char* label_for(UpdateTarget target) const;
    UpdateTarget target_for_step(SetupStep step) const;
    std::optional<vesc::FWVersion::Response>& fw_for(UpdateTarget target);
    const std::optional<vesc::FWVersion::Response>& fw_for(UpdateTarget target) const;
    void latest_for(UpdateTarget target, uint8_t& major, uint8_t& minor) const;

    // FW step helpers
    void begin_fw_check(SetupStep step);
    void send_fw_query(UpdateTarget target);
    void send_fw_update(UpdateTarget target);
    bool check_and_report_fw(SetupStep step, UpdateTarget target);
    void handle_fw_response(const uint8_t* data, size_t len);
    void handle_fw_update_response(vesc::CommPacketID cmd);
    void advance_to_next_fw_check();

    // Factory reset helpers
    void send_reset_command();
    void handle_reset_response(vesc::CommPacketID cmd, const uint8_t* data, size_t len);

    // Refloat install helpers
    void send_install_command();
    void handle_install_response(vesc::CommPacketID cmd, const uint8_t* data, size_t len);
};

} // namespace nosedive
