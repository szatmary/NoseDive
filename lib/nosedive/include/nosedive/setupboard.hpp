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
// The wizard sends VESC commands via a send callback and advances
// when it receives the expected response. The engine feeds responses
// to handle_response(). The wizard fires a state callback on each
// step transition so the UI can update.

#include <vesc/commands.hpp>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace nosedive {

enum class SetupStep : uint8_t {
    Idle,
    CheckFWExpress,     // Check VESC Express (BLE bridge) firmware
    CheckFWBMS,         // Check BMS firmware
    CheckFWVESC,        // Check VESC motor controller firmware
    UpdateFW,           // Firmware update in progress
    WaitReconnect,      // Waiting for board to reconnect after update
    InstallRefloat,     // Install Refloat package if missing
    DetectBattery,      // Read battery voltage, detect cell count
    DetectFootpads,     // Check footpad ADC sensors
    CalibrateIMU,       // Calibrate gyroscope and accelerometer
    DetectMotor,        // Run motor detection (R/L/flux linkage)
    ConfigureWheel,     // Set wheel diameter
    Done,
};

// Which device is being updated
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

struct SetupState {
    SetupStep step = SetupStep::Idle;
    std::string error;    // empty = no error
    std::string detail;   // what's happening / what was detected
};

/// Callback: wizard state changed, UI should update.
using SetupCallback = std::function<void(const SetupState&)>;

/// Callback: wizard wants to send a VESC payload.
using SetupSendCallback = std::function<void(const std::vector<uint8_t>& payload)>;

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

    /// Start firmware update for the current CheckFW step.
    /// Transitions to UpdateFW → WaitReconnect.
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

    /// Whether Refloat is already installed.
    bool has_refloat = false;

private:
    SetupState state_;
    SetupCallback state_cb_;
    SetupSendCallback send_cb_;

    void set_step(SetupStep step, const std::string& detail = "");
    void set_error(const std::string& error);
    void advance();
    void send_commands_for_step();
    void fire_callback();

    // Track which CAN devices we've checked
    bool express_checked_ = false;
    bool bms_checked_ = false;

    // Firmware update state
    UpdateTarget update_target_ = UpdateTarget::None;
    std::optional<vesc::FWVersion::Response> express_fw_;
    std::optional<vesc::FWVersion::Response> bms_fw_;

    // Per-target helpers (consolidate Express/BMS/VESC branching)
    std::optional<uint8_t> find_can_id(UpdateTarget target) const;
    const char* label_for(UpdateTarget target) const;
    UpdateTarget target_for_step(SetupStep step) const;
    SetupStep check_step_for(UpdateTarget target) const;
    std::optional<vesc::FWVersion::Response>& fw_for(UpdateTarget target);
    const std::optional<vesc::FWVersion::Response>& fw_for(UpdateTarget target) const;
    void latest_for(UpdateTarget target, uint8_t& major, uint8_t& minor) const;

    // Send a FW version query to the given target
    void send_fw_query(UpdateTarget target);
    // Send erase+write commands to the given target
    void send_fw_update(UpdateTarget target);

    // Check FW version, report to UI, return true if outdated
    bool check_and_report_fw(SetupStep step, UpdateTarget target);

    // Common logic: advance from a CheckFW step to the next one or to VESC check
    void advance_to_next_fw_check();
};

} // namespace nosedive
