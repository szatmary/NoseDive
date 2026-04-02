#include "nosedive/setupboard.hpp"
#include <cstdio>
#include <string>

namespace nosedive {

static std::string fw_detail(const char* prefix, const vesc::FWVersion::Response& fw) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s FW %d.%02d", prefix, fw.major, fw.minor);
    std::string result = buf;
    if (!fw.hw_name.empty()) {
        result += " \xe2\x80\x94 "; // em-dash
        result += fw.hw_name;
    }
    return result;
}


bool SetupBoard::is_running() const {
    return state_.step != SetupStep::Idle && state_.step != SetupStep::Done;
}

void SetupBoard::start() {
    state_ = {};
    express_checked_ = false;
    bms_checked_ = false;

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
                set_step(SetupStep::CheckFWVESC, fw_detail("VESC", *main_fw));
                advance();
            } else {
                set_step(SetupStep::CheckFWVESC, "Checking VESC firmware...");
                send_commands_for_step();
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

void SetupBoard::abort() {
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
                set_step(SetupStep::CheckFWVESC, fw_detail("VESC", *main_fw));
                advance(); // already have FW info, skip to next
                return;
            }
            set_step(SetupStep::CheckFWVESC, "Checking VESC firmware...");
        }
        break;

    case SetupStep::CheckFWBMS:
        bms_checked_ = true;
        if (main_fw) {
            set_step(SetupStep::CheckFWVESC, fw_detail("VESC", *main_fw));
            advance();
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
                set_step(SetupStep::CheckFWExpress, fw_detail("Express", *fw));
                advance();
            } else {
                set_error("Could not read Express firmware");
            }
        }
        break;

    case SetupStep::CheckFWBMS:
        if (cmd == vesc::CommPacketID::FWVersion) {
            if (auto fw = vesc::FWVersion::Response::decode(data, len)) {
                set_step(SetupStep::CheckFWBMS, fw_detail("BMS", *fw));
                advance();
            } else {
                set_error("Could not read BMS firmware");
            }
        }
        break;

    case SetupStep::CheckFWVESC:
        if (cmd == vesc::CommPacketID::FWVersion) {
            if (auto fw = vesc::FWVersion::Response::decode(data, len)) {
                main_fw = *fw;
                set_step(SetupStep::CheckFWVESC, fw_detail("VESC", *fw));
                advance();
            } else {
                set_error("Could not read VESC firmware");
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
    // Prefer the conventional BMS ID (10)
    for (auto id : can_device_ids) {
        if (id == 10) return id;
    }
    // Fallback: accept any non-Express CAN device in range
    for (auto id : can_device_ids) {
        if (id > 10 && id < 250) return id;
    }
    return std::nullopt;
}

} // namespace nosedive
