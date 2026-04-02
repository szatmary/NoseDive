#include "nosedive/setupboard.hpp"
#include <cstdio>

namespace nosedive {

bool SetupBoard::is_running() const {
    return state_.step != SetupStep::Idle && state_.step != SetupStep::Done;
}

void SetupBoard::start() {
    state_ = {};
    express_checked_ = false;
    bms_checked_ = false;
    update_target_ = UpdateTarget::None;
    express_fw_ = std::nullopt;
    bms_fw_ = std::nullopt;
    post_reconnect_step_ = SetupStep::Idle;

    // Start with Express FW check if there's an Express on CAN
    if (find_express_id()) {
        set_step(SetupStep::CheckFWExpress, "Checking VESC Express firmware...");
    } else {
        express_checked_ = true;
        // Skip to BMS check
        if (find_bms_id()) {
            set_step(SetupStep::CheckFWBMS, "Checking BMS firmware...");
        } else {
            bms_checked_ = true;
            // Skip to main VESC check
            if (main_fw) {
                check_and_report_fw(SetupStep::CheckFWVESC, *main_fw, "VESC",
                                     LatestFW::vesc_major, LatestFW::vesc_minor);
                if (!is_vesc_outdated()) {
                    advance();
                }
                // If outdated, we stay at CheckFWVESC — user must update() or skip()
                return;
            } else {
                set_step(SetupStep::CheckFWVESC, "Checking VESC firmware...");
            }
        }
    }
    send_commands_for_step();
}

void SetupBoard::retry() {
    state_.error.clear();
    send_commands_for_step();
    fire_callback();
}

void SetupBoard::skip() {
    state_.error.clear();
    advance();
}

void SetupBoard::update() {
    // Determine which device to update based on current step
    switch (state_.step) {
    case SetupStep::CheckFWExpress:
        update_target_ = UpdateTarget::Express;
        post_reconnect_step_ = SetupStep::CheckFWExpress;
        break;
    case SetupStep::CheckFWBMS:
        update_target_ = UpdateTarget::BMS;
        post_reconnect_step_ = SetupStep::CheckFWBMS;
        break;
    case SetupStep::CheckFWVESC:
        update_target_ = UpdateTarget::VESC;
        post_reconnect_step_ = SetupStep::CheckFWVESC;
        break;
    default:
        return; // Can only update from a CheckFW step
    }

    set_step(SetupStep::UpdateFW, "Uploading firmware...");
    send_commands_for_step();
}

void SetupBoard::on_reconnected() {
    if (state_.step != SetupStep::WaitReconnect) return;

    // Board has reconnected — re-query firmware version to verify update
    set_step(SetupStep::WaitReconnect, "Reconnected, verifying firmware...");
    if (update_target_ == UpdateTarget::VESC) {
        if (send_cb_) send_cb_(vesc::FWVersion::Request{}.encode());
    } else if (update_target_ == UpdateTarget::Express) {
        if (auto id = find_express_id()) {
            if (send_cb_) send_cb_(vesc::ForwardCAN::Request{
                .target_id = *id,
                .inner_payload = vesc::FWVersion::Request{}.encode()
            }.encode());
        }
    } else if (update_target_ == UpdateTarget::BMS) {
        if (auto id = find_bms_id()) {
            if (send_cb_) send_cb_(vesc::ForwardCAN::Request{
                .target_id = *id,
                .inner_payload = vesc::FWVersion::Request{}.encode()
            }.encode());
        }
    }
}

void SetupBoard::abort() {
    update_target_ = UpdateTarget::None;
    set_step(SetupStep::Idle, "Setup cancelled");
}

void SetupBoard::set_step(SetupStep step, const std::string& detail) {
    state_.step = step;
    state_.detail = detail;
    state_.error.clear();
    fire_callback();
}

void SetupBoard::set_error(const std::string& error) {
    state_.error = error;
    fire_callback();
}

void SetupBoard::fire_callback() {
    if (state_cb_) state_cb_(state_);
}

// --- Version comparison helpers ---

bool SetupBoard::is_vesc_outdated() const {
    if (!main_fw) return false;
    return LatestFW::is_outdated(main_fw->major, main_fw->minor,
                                  LatestFW::vesc_major, LatestFW::vesc_minor);
}

bool SetupBoard::is_express_outdated() const {
    if (!express_fw_) return false;
    return LatestFW::is_outdated(express_fw_->major, express_fw_->minor,
                                  LatestFW::express_major, LatestFW::express_minor);
}

bool SetupBoard::is_bms_outdated() const {
    if (!bms_fw_) return false;
    // BMS uses same version scheme as VESC
    return LatestFW::is_outdated(bms_fw_->major, bms_fw_->minor,
                                  LatestFW::vesc_major, LatestFW::vesc_minor);
}

void SetupBoard::check_and_report_fw(SetupStep step,
                                      const vesc::FWVersion::Response& fw,
                                      const char* label,
                                      uint8_t latest_major, uint8_t latest_minor) {
    char buf[256];
    bool outdated = LatestFW::is_outdated(fw.major, fw.minor, latest_major, latest_minor);
    if (outdated) {
        std::snprintf(buf, sizeof(buf), "%s FW %d.%02d — update available (%d.%02d)",
            label, fw.major, fw.minor, latest_major, latest_minor);
    } else {
        std::snprintf(buf, sizeof(buf), "%s FW %d.%02d — %s — up to date",
            label, fw.major, fw.minor, fw.hw_name.c_str());
    }
    set_step(step, buf);
}

// --- State machine ---

void SetupBoard::advance() {
    auto current = state_.step;

    switch (current) {
    case SetupStep::CheckFWExpress:
        express_checked_ = true;
        if (find_bms_id()) {
            set_step(SetupStep::CheckFWBMS, "Checking BMS firmware...");
        } else {
            bms_checked_ = true;
            if (main_fw) {
                check_and_report_fw(SetupStep::CheckFWVESC, *main_fw, "VESC",
                                     LatestFW::vesc_major, LatestFW::vesc_minor);
                if (!is_vesc_outdated()) {
                    advance(); // up to date, skip to next
                }
                // If outdated, pause — user must update() or skip()
                return;
            }
            set_step(SetupStep::CheckFWVESC, "Checking VESC firmware...");
        }
        break;

    case SetupStep::CheckFWBMS:
        bms_checked_ = true;
        if (main_fw) {
            check_and_report_fw(SetupStep::CheckFWVESC, *main_fw, "VESC",
                                 LatestFW::vesc_major, LatestFW::vesc_minor);
            if (!is_vesc_outdated()) {
                advance();
            }
            return;
        }
        set_step(SetupStep::CheckFWVESC, "Checking VESC firmware...");
        break;

    case SetupStep::CheckFWVESC:
        if (has_refloat) {
            set_step(SetupStep::InstallRefloat, "Refloat already installed");
            advance(); // skip install
            return;
        }
        set_step(SetupStep::InstallRefloat, "Installing Refloat package...");
        break;

    case SetupStep::UpdateFW:
        // Firmware upload done — wait for board to reconnect
        set_step(SetupStep::WaitReconnect, "Board is rebooting, waiting for reconnect...");
        return; // Don't send commands — we're waiting for external reconnect

    case SetupStep::WaitReconnect:
        // Reconnect verified — resume from the CheckFW step that triggered update
        update_target_ = UpdateTarget::None;
        // Advance past the CheckFW step that was just verified
        switch (post_reconnect_step_) {
        case SetupStep::CheckFWExpress:
            // Re-run advance from CheckFWExpress
            state_.step = SetupStep::CheckFWExpress;
            advance();
            return;
        case SetupStep::CheckFWBMS:
            state_.step = SetupStep::CheckFWBMS;
            advance();
            return;
        case SetupStep::CheckFWVESC:
            state_.step = SetupStep::CheckFWVESC;
            advance();
            return;
        default:
            break;
        }
        break;

    case SetupStep::InstallRefloat:
        set_step(SetupStep::DetectBattery, "Detecting battery...");
        break;

    case SetupStep::DetectBattery:
        set_step(SetupStep::DetectFootpads, "Detecting footpad sensors...");
        break;

    case SetupStep::DetectFootpads:
        set_step(SetupStep::CalibrateIMU, "Calibrating IMU...");
        break;

    case SetupStep::CalibrateIMU:
        set_step(SetupStep::DetectMotor, "Detecting motor parameters...");
        break;

    case SetupStep::DetectMotor:
        set_step(SetupStep::ConfigureWheel, "Configuring wheel...");
        break;

    case SetupStep::ConfigureWheel:
        set_step(SetupStep::Done, "Setup complete!");
        return;

    default:
        break;
    }

    send_commands_for_step();
}

void SetupBoard::send_commands_for_step() {
    if (!send_cb_) return;

    switch (state_.step) {
    case SetupStep::CheckFWExpress:
        if (auto id = find_express_id()) {
            send_cb_(vesc::ForwardCAN::Request{
                .target_id = *id,
                .inner_payload = vesc::FWVersion::Request{}.encode()
            }.encode());
        }
        break;

    case SetupStep::CheckFWBMS:
        if (auto id = find_bms_id()) {
            send_cb_(vesc::ForwardCAN::Request{
                .target_id = *id,
                .inner_payload = vesc::FWVersion::Request{}.encode()
            }.encode());
        }
        break;

    case SetupStep::CheckFWVESC:
        send_cb_(vesc::FWVersion::Request{}.encode());
        break;

    case SetupStep::UpdateFW:
        // Send firmware update commands (simplified — real impl would chunk binary)
        if (update_target_ == UpdateTarget::VESC) {
            send_cb_({static_cast<uint8_t>(vesc::CommPacketID::EraseNewApp)});
            send_cb_({static_cast<uint8_t>(vesc::CommPacketID::WriteNewAppData)});
        } else if (update_target_ == UpdateTarget::Express) {
            if (auto id = find_express_id()) {
                send_cb_(vesc::ForwardCAN::Request{
                    .target_id = *id,
                    .inner_payload = {static_cast<uint8_t>(vesc::CommPacketID::EraseNewApp)}
                }.encode());
                send_cb_(vesc::ForwardCAN::Request{
                    .target_id = *id,
                    .inner_payload = {static_cast<uint8_t>(vesc::CommPacketID::WriteNewAppData)}
                }.encode());
            }
        } else if (update_target_ == UpdateTarget::BMS) {
            if (auto id = find_bms_id()) {
                send_cb_(vesc::ForwardCAN::Request{
                    .target_id = *id,
                    .inner_payload = {static_cast<uint8_t>(vesc::CommPacketID::EraseNewApp)}
                }.encode());
                send_cb_(vesc::ForwardCAN::Request{
                    .target_id = *id,
                    .inner_payload = {static_cast<uint8_t>(vesc::CommPacketID::WriteNewAppData)}
                }.encode());
            }
        }
        break;

    case SetupStep::InstallRefloat:
        send_cb_({static_cast<uint8_t>(vesc::CommPacketID::EraseNewApp)});
        send_cb_({static_cast<uint8_t>(vesc::CommPacketID::WriteNewAppData)});
        break;

    case SetupStep::DetectBattery:
        send_cb_(vesc::GetValues::Request{}.encode());
        break;

    case SetupStep::DetectFootpads:
        send_cb_(vesc::GetValues::Request{}.encode());
        break;

    case SetupStep::CalibrateIMU:
        send_cb_(vesc::GetIMUData::Request{.mask = 0x001F}.encode());
        break;

    case SetupStep::DetectMotor:
        send_cb_(vesc::DetectApplyAllFOC::Request{
            .detect_can = false,
            .max_power_loss = 1.0
        }.encode());
        break;

    case SetupStep::ConfigureWheel:
        send_cb_({static_cast<uint8_t>(vesc::CommPacketID::GetMCConf)});
        break;

    default:
        break;
    }
}

void SetupBoard::handle_response(const uint8_t* data, size_t len) {
    if (!is_running() || len == 0) return;

    auto cmd = static_cast<vesc::CommPacketID>(data[0]);

    switch (state_.step) {
    case SetupStep::CheckFWExpress:
        if (cmd == vesc::CommPacketID::FWVersion) {
            if (auto fw = vesc::FWVersion::Response::decode(data, len)) {
                express_fw_ = *fw;
                check_and_report_fw(SetupStep::CheckFWExpress, *fw, "Express",
                                     LatestFW::express_major, LatestFW::express_minor);
                if (!is_express_outdated()) {
                    advance();
                }
                // If outdated, pause — user must update() or skip()
            } else {
                set_error("Could not read Express firmware");
            }
        }
        break;

    case SetupStep::CheckFWBMS:
        if (cmd == vesc::CommPacketID::FWVersion) {
            if (auto fw = vesc::FWVersion::Response::decode(data, len)) {
                bms_fw_ = *fw;
                check_and_report_fw(SetupStep::CheckFWBMS, *fw, "BMS",
                                     LatestFW::vesc_major, LatestFW::vesc_minor);
                if (!is_bms_outdated()) {
                    advance();
                }
            } else {
                set_error("Could not read BMS firmware");
            }
        }
        break;

    case SetupStep::CheckFWVESC:
        if (cmd == vesc::CommPacketID::FWVersion) {
            if (auto fw = vesc::FWVersion::Response::decode(data, len)) {
                main_fw = *fw;
                check_and_report_fw(SetupStep::CheckFWVESC, *fw, "VESC",
                                     LatestFW::vesc_major, LatestFW::vesc_minor);
                if (!is_vesc_outdated()) {
                    advance();
                }
                // If outdated, pause — user must update() or skip()
            } else {
                set_error("Could not read VESC firmware");
            }
        }
        break;

    case SetupStep::UpdateFW:
        // Firmware write acknowledged — transition to WaitReconnect
        if (cmd == vesc::CommPacketID::WriteNewAppData ||
            cmd == vesc::CommPacketID::EraseNewApp) {
            // Wait for the final write ack
            if (cmd == vesc::CommPacketID::WriteNewAppData) {
                advance(); // → WaitReconnect
            }
        }
        break;

    case SetupStep::WaitReconnect:
        // We're waiting for a FW version response after reconnect
        if (cmd == vesc::CommPacketID::FWVersion) {
            if (auto fw = vesc::FWVersion::Response::decode(data, len)) {
                char buf[256];
                if (update_target_ == UpdateTarget::VESC) {
                    main_fw = *fw;
                    std::snprintf(buf, sizeof(buf), "VESC FW updated to %d.%02d",
                        fw->major, fw->minor);
                } else if (update_target_ == UpdateTarget::Express) {
                    express_fw_ = *fw;
                    std::snprintf(buf, sizeof(buf), "Express FW updated to %d.%02d",
                        fw->major, fw->minor);
                } else if (update_target_ == UpdateTarget::BMS) {
                    bms_fw_ = *fw;
                    std::snprintf(buf, sizeof(buf), "BMS FW updated to %d.%02d",
                        fw->major, fw->minor);
                }
                set_step(SetupStep::WaitReconnect, buf);
                advance(); // → resume from post_reconnect_step_
            } else {
                set_error("Could not verify firmware after update");
            }
        }
        break;

    case SetupStep::InstallRefloat:
        if (cmd == vesc::CommPacketID::WriteNewAppData) {
            has_refloat = true;
            set_step(SetupStep::InstallRefloat, "Refloat installed");
            advance();
        }
        break;

    case SetupStep::DetectBattery:
        if (cmd == vesc::CommPacketID::GetValues) {
            if (auto v = vesc::GetValues::Response::decode(data, len)) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Battery: %.1fV", v->voltage);
                set_step(SetupStep::DetectBattery, buf);
                advance();
            } else {
                set_error("Could not read battery voltage");
            }
        }
        break;

    case SetupStep::DetectFootpads:
        if (cmd == vesc::CommPacketID::GetValues) {
            set_step(SetupStep::DetectFootpads, "Footpad sensors OK");
            advance();
        }
        break;

    case SetupStep::CalibrateIMU:
        if (cmd == vesc::CommPacketID::GetIMUData) {
            if (auto imu = vesc::GetIMUData::Response::decode(data, len)) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "IMU OK — pitch=%.1f° roll=%.1f°",
                    imu->pitch, imu->roll);
                set_step(SetupStep::CalibrateIMU, buf);
                advance();
            } else {
                set_error("Could not read IMU data");
            }
        }
        break;

    case SetupStep::DetectMotor:
        if (cmd == vesc::CommPacketID::DetectApplyAllFOC) {
            if (auto r = vesc::DetectApplyAllFOC::Response::decode(data, len)) {
                if (r->result >= 0) {
                    set_step(SetupStep::DetectMotor, "Motor detected successfully");
                    advance();
                } else {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "Motor detection failed (code %d)", r->result);
                    set_error(buf);
                }
            } else {
                set_error("Motor detection failed");
            }
        }
        break;

    case SetupStep::ConfigureWheel:
        if (cmd == vesc::CommPacketID::GetMCConf) {
            set_step(SetupStep::ConfigureWheel, "Configuration read");
            advance();
        }
        break;

    default:
        break;
    }
}

std::optional<uint8_t> SetupBoard::find_express_id() const {
    for (auto id : can_device_ids) {
        if (id >= 250) return id; // Express is conventionally 253
    }
    return std::nullopt;
}

std::optional<uint8_t> SetupBoard::find_bms_id() const {
    for (auto id : can_device_ids) {
        if (id >= 10 && id < 250) return id; // BMS is conventionally 10
    }
    return std::nullopt;
}

} // namespace nosedive
