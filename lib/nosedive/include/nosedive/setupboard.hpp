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
    InstallRefloat,     // Install Refloat package if missing
    DetectBattery,      // Read battery voltage, detect cell count
    DetectFootpads,     // Check footpad ADC sensors
    CalibrateIMU,       // Calibrate gyroscope and accelerometer
    DetectMotor,        // Run motor detection (R/L/flux linkage)
    ConfigureWheel,     // Set wheel diameter
    Done,
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

    // Find a CAN device ID by convention
    // Express is typically ID 253, BMS is typically ID 10
    std::optional<uint8_t> find_express_id() const;
    std::optional<uint8_t> find_bms_id() const;
};

} // namespace nosedive
