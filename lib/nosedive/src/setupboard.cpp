#include "nosedive/setupboard.hpp"
#include <cstdio>

namespace nosedive {

bool SetupBoard::is_running() const {
    return state_.step != SetupStep::Idle && state_.step != SetupStep::Done;
}

// --- Per-target helpers ---

std::optional<uint8_t> SetupBoard::find_can_id(UpdateTarget target) const {
    for (auto id : can_device_ids) {
        if (target == UpdateTarget::Express && id >= 250) return id;
        if (target == UpdateTarget::BMS && id >= 10 && id < 250) return id;
    }
    return std::nullopt;
}

const char* SetupBoard::label_for(UpdateTarget target) const {
    switch (target) {
    case UpdateTarget::Express: return "Express";
    case UpdateTarget::BMS:     return "BMS";
    case UpdateTarget::VESC:    return "VESC";
    default:                    return "Unknown";
    }
}

UpdateTarget SetupBoard::target_for_step(SetupStep step) const {
    switch (step) {
    case SetupStep::CheckFWExpress: return UpdateTarget::Express;
    case SetupStep::CheckFWBMS:     return UpdateTarget::BMS;
    case SetupStep::CheckFWVESC:    return UpdateTarget::VESC;
    default:                        return UpdateTarget::None;
    }
}

SetupStep SetupBoard::check_step_for(UpdateTarget target) const {
    switch (target) {
    case UpdateTarget::Express: return SetupStep::CheckFWExpress;
    case UpdateTarget::BMS:     return SetupStep::CheckFWBMS;
    case UpdateTarget::VESC:    return SetupStep::CheckFWVESC;
    default:                    return SetupStep::Idle;
    }
}

std::optional<vesc::FWVersion::Response>& SetupBoard::fw_for(UpdateTarget target) {
    if (target == UpdateTarget::Express) return express_fw_;
    if (target == UpdateTarget::BMS) return bms_fw_;
    return main_fw;
}

const std::optional<vesc::FWVersion::Response>& SetupBoard::fw_for(UpdateTarget target) const {
    if (target == UpdateTarget::Express) return express_fw_;
    if (target == UpdateTarget::BMS) return bms_fw_;
    return main_fw;
}

void SetupBoard::latest_for(UpdateTarget target, uint8_t& major, uint8_t& minor) const {
    if (target == UpdateTarget::Express) {
        major = LatestFW::express_major;
        minor = LatestFW::express_minor;
    } else {
        major = LatestFW::vesc_major;
        minor = LatestFW::vesc_minor;
    }
}

void SetupBoard::send_fw_query(UpdateTarget target) {
    if (!send_cb_) return;
    auto fw_req = vesc::FWVersion::Request{}.encode();
    if (target == UpdateTarget::VESC) {
        send_cb_(fw_req);
    } else if (auto id = find_can_id(target)) {
        send_cb_(vesc::ForwardCAN::Request{
            .target_id = *id, .inner_payload = fw_req
        }.encode());
    }
}

void SetupBoard::send_fw_update(UpdateTarget target) {
    if (!send_cb_) return;
    auto erase = std::vector<uint8_t>{static_cast<uint8_t>(vesc::CommPacketID::EraseNewApp)};
    auto write = std::vector<uint8_t>{static_cast<uint8_t>(vesc::CommPacketID::WriteNewAppData)};
    if (target == UpdateTarget::VESC) {
        send_cb_(erase);
        send_cb_(write);
    } else if (auto id = find_can_id(target)) {
        send_cb_(vesc::ForwardCAN::Request{.target_id = *id, .inner_payload = erase}.encode());
        send_cb_(vesc::ForwardCAN::Request{.target_id = *id, .inner_payload = write}.encode());
    }
}

bool SetupBoard::check_and_report_fw(SetupStep step, UpdateTarget target) {
    auto& fw = fw_for(target);
    if (!fw) return false;
    uint8_t latest_major, latest_minor;
    latest_for(target, latest_major, latest_minor);
    bool outdated = LatestFW::is_outdated(fw->major, fw->minor, latest_major, latest_minor);

    char buf[256];
    if (outdated) {
        std::snprintf(buf, sizeof(buf), "%s FW %d.%02d — update available (%d.%02d)",
            label_for(target), fw->major, fw->minor, latest_major, latest_minor);
    } else {
        std::snprintf(buf, sizeof(buf), "%s FW %d.%02d — %s — up to date",
            label_for(target), fw->major, fw->minor, fw->hw_name.c_str());
    }
    set_step(step, buf);
    return outdated;
}

// Common logic for advancing through the FW check chain.
// Called after completing CheckFWExpress or CheckFWBMS.
void SetupBoard::advance_to_next_fw_check() {
    auto current = state_.step;

    if (current == SetupStep::CheckFWExpress) {
        express_checked_ = true;
        if (find_can_id(UpdateTarget::BMS)) {
            set_step(SetupStep::CheckFWBMS, "Checking BMS firmware...");
            send_commands_for_step();
            return;
        }
        bms_checked_ = true;
    } else if (current == SetupStep::CheckFWBMS) {
        bms_checked_ = true;
    }

    // Fall through to VESC check
    if (main_fw) {
        if (!check_and_report_fw(SetupStep::CheckFWVESC, UpdateTarget::VESC)) {
            advance(); // up to date, continue
        }
        // If outdated, pause — user must update() or skip()
    } else {
        set_step(SetupStep::CheckFWVESC, "Checking VESC firmware...");
        send_commands_for_step();
    }
}

// --- Public API ---

void SetupBoard::start() {
    state_ = {};
    express_checked_ = false;
    bms_checked_ = false;
    update_target_ = UpdateTarget::None;
    express_fw_ = std::nullopt;
    bms_fw_ = std::nullopt;

    if (find_can_id(UpdateTarget::Express)) {
        set_step(SetupStep::CheckFWExpress, "Checking VESC Express firmware...");
        send_commands_for_step();
    } else if (find_can_id(UpdateTarget::BMS)) {
        express_checked_ = true;
        set_step(SetupStep::CheckFWBMS, "Checking BMS firmware...");
        send_commands_for_step();
    } else {
        express_checked_ = true;
        bms_checked_ = true;
        if (main_fw) {
            if (!check_and_report_fw(SetupStep::CheckFWVESC, UpdateTarget::VESC)) {
                advance();
            }
        } else {
            set_step(SetupStep::CheckFWVESC, "Checking VESC firmware...");
            send_commands_for_step();
        }
    }
}

void SetupBoard::retry() {
    state_.error.clear();
    send_commands_for_step();
    fire_callback();
}

void SetupBoard::skip() {
    // Cannot skip Refloat installation
    if (state_.step == SetupStep::InstallRefloat) return;
    state_.error.clear();
    advance();
}

void SetupBoard::update() {
    update_target_ = target_for_step(state_.step);
    if (update_target_ == UpdateTarget::None) return;

    set_step(SetupStep::UpdateFW, "Uploading firmware...");
    send_commands_for_step();
}

void SetupBoard::on_reconnected() {
    if (state_.step != SetupStep::WaitReconnect) return;

    set_step(SetupStep::WaitReconnect, "Reconnected, verifying firmware...");
    send_fw_query(update_target_);
}

void SetupBoard::abort() {
    update_target_ = UpdateTarget::None;
    set_step(SetupStep::Idle, "Setup cancelled");
}

// --- Internal helpers ---

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

// --- State machine ---

void SetupBoard::advance() {
    switch (state_.step) {
    case SetupStep::CheckFWExpress:
    case SetupStep::CheckFWBMS:
        advance_to_next_fw_check();
        return;

    case SetupStep::CheckFWVESC:
        if (has_refloat) {
            set_step(SetupStep::InstallRefloat, "Refloat already installed");
            advance();
            return;
        }
        set_step(SetupStep::InstallRefloat, "Installing Refloat package...");
        break;

    case SetupStep::UpdateFW:
        set_step(SetupStep::WaitReconnect, "Board is rebooting, waiting for reconnect...");
        return; // Don't send commands — waiting for external reconnect

    case SetupStep::WaitReconnect: {
        // Reconnect verified — resume from the CheckFW step that triggered update
        auto resume = check_step_for(update_target_);
        update_target_ = UpdateTarget::None;
        state_.step = resume;
        advance();
        return;
    }

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
        send_fw_query(UpdateTarget::Express);
        break;

    case SetupStep::CheckFWBMS:
        send_fw_query(UpdateTarget::BMS);
        break;

    case SetupStep::CheckFWVESC:
        send_fw_query(UpdateTarget::VESC);
        break;

    case SetupStep::UpdateFW:
        send_fw_update(update_target_);
        break;

    case SetupStep::InstallRefloat:
        install_phase_ = InstallPhase::LispErase;
        install_offset_ = 0;
        send_install_command();
        break;

    case SetupStep::DetectBattery:
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
    case SetupStep::CheckFWBMS:
    case SetupStep::CheckFWVESC: {
        if (cmd != vesc::CommPacketID::FWVersion) break;
        auto fw = vesc::FWVersion::Response::decode(data, len);
        if (!fw) {
            set_error(std::string("Could not read ") + label_for(target_for_step(state_.step)) + " firmware");
            break;
        }
        auto target = target_for_step(state_.step);
        fw_for(target) = *fw;
        if (!check_and_report_fw(state_.step, target)) {
            advance(); // up to date
        }
        // If outdated, pause — user must update() or skip()
        break;
    }

    case SetupStep::UpdateFW:
        if (cmd == vesc::CommPacketID::WriteNewAppData) {
            advance(); // → WaitReconnect
        }
        break;

    case SetupStep::WaitReconnect:
        if (cmd == vesc::CommPacketID::FWVersion) {
            auto fw = vesc::FWVersion::Response::decode(data, len);
            if (!fw) {
                set_error("Could not verify firmware after update");
                break;
            }
            fw_for(update_target_) = *fw;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s FW updated to %d.%02d",
                label_for(update_target_), fw->major, fw->minor);
            set_step(SetupStep::WaitReconnect, buf);
            advance(); // → resume from check step
        }
        break;

    case SetupStep::InstallRefloat:
        handle_install_response(cmd, data, len);
        break;

    case SetupStep::DetectBattery:
        if (cmd == vesc::CommPacketID::GetValues) {
            auto v = vesc::GetValues::Response::decode(data, len);
            if (!v) { set_error("Could not read battery voltage"); break; }
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Battery: %.1fV", v->voltage);
            set_step(SetupStep::DetectBattery, buf);
            advance();
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
            auto imu = vesc::GetIMUData::Response::decode(data, len);
            if (!imu) { set_error("Could not read IMU data"); break; }
            char buf[128];
            std::snprintf(buf, sizeof(buf), "IMU OK — pitch=%.1f° roll=%.1f°",
                imu->pitch, imu->roll);
            set_step(SetupStep::CalibrateIMU, buf);
            advance();
        }
        break;

    case SetupStep::DetectMotor:
        if (cmd == vesc::CommPacketID::DetectApplyAllFOC) {
            auto r = vesc::DetectApplyAllFOC::Response::decode(data, len);
            if (!r) { set_error("Motor detection failed"); break; }
            if (r->result >= 0) {
                set_step(SetupStep::DetectMotor, "Motor detected successfully");
                advance();
            } else {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Motor detection failed (code %d)", r->result);
                set_error(buf);
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

// --- Refloat install sub-state machine ---

void SetupBoard::send_install_command() {
    if (!send_cb_) return;

    switch (install_phase_) {
    case InstallPhase::LispErase:
        set_step(SetupStep::InstallRefloat, "Erasing Lisp code...");
        send_cb_({static_cast<uint8_t>(vesc::CommPacketID::LispEraseCode)});
        break;

    case InstallPhase::LispWrite: {
        if (!refloat_package || refloat_package->lisp_data.empty()) {
            set_error("No Refloat package loaded");
            return;
        }
        const auto& data = refloat_package->lisp_data;
        size_t remaining = data.size() - install_offset_;
        size_t chunk = std::min(remaining, kUploadChunkSize);

        char buf[128];
        std::snprintf(buf, sizeof(buf), "Uploading Lisp code... %zu%%",
            install_offset_ * 100 / data.size());
        set_step(SetupStep::InstallRefloat, buf);

        // [cmd(1), offset(4 BE), data(N)]
        std::vector<uint8_t> pkt;
        pkt.reserve(5 + chunk);
        pkt.push_back(static_cast<uint8_t>(vesc::CommPacketID::LispWriteCode));
        pkt.push_back(static_cast<uint8_t>((install_offset_ >> 24) & 0xFF));
        pkt.push_back(static_cast<uint8_t>((install_offset_ >> 16) & 0xFF));
        pkt.push_back(static_cast<uint8_t>((install_offset_ >> 8) & 0xFF));
        pkt.push_back(static_cast<uint8_t>(install_offset_ & 0xFF));
        pkt.insert(pkt.end(), data.data() + install_offset_,
                   data.data() + install_offset_ + chunk);
        send_cb_(pkt);
        break;
    }

    case InstallPhase::QmlErase:
        set_step(SetupStep::InstallRefloat, "Erasing QML UI...");
        send_cb_({static_cast<uint8_t>(vesc::CommPacketID::QmluiErase)});
        break;

    case InstallPhase::QmlWrite: {
        if (!refloat_package || refloat_package->qml_data.empty()) {
            // No QML data — skip to SetRunning
            install_phase_ = InstallPhase::SetRunning;
            send_install_command();
            return;
        }
        const auto& data = refloat_package->qml_data;
        size_t remaining = data.size() - install_offset_;
        size_t chunk = std::min(remaining, kUploadChunkSize);

        char buf[128];
        std::snprintf(buf, sizeof(buf), "Uploading QML UI... %zu%%",
            install_offset_ * 100 / data.size());
        set_step(SetupStep::InstallRefloat, buf);

        std::vector<uint8_t> pkt;
        pkt.reserve(5 + chunk);
        pkt.push_back(static_cast<uint8_t>(vesc::CommPacketID::QmluiWrite));
        pkt.push_back(static_cast<uint8_t>((install_offset_ >> 24) & 0xFF));
        pkt.push_back(static_cast<uint8_t>((install_offset_ >> 16) & 0xFF));
        pkt.push_back(static_cast<uint8_t>((install_offset_ >> 8) & 0xFF));
        pkt.push_back(static_cast<uint8_t>(install_offset_ & 0xFF));
        pkt.insert(pkt.end(), data.data() + install_offset_,
                   data.data() + install_offset_ + chunk);
        send_cb_(pkt);
        break;
    }

    case InstallPhase::SetRunning:
        set_step(SetupStep::InstallRefloat, "Starting Lisp runtime...");
        send_cb_({static_cast<uint8_t>(vesc::CommPacketID::LispSetRunning), 1});
        break;

    case InstallPhase::Done:
        break;
    }
}

void SetupBoard::handle_install_response(vesc::CommPacketID cmd,
                                          const uint8_t* data, size_t len) {
    switch (install_phase_) {
    case InstallPhase::LispErase:
        if (cmd == vesc::CommPacketID::LispEraseCode) {
            install_phase_ = InstallPhase::LispWrite;
            install_offset_ = 0;
            send_install_command();
        }
        break;

    case InstallPhase::LispWrite:
        if (cmd == vesc::CommPacketID::LispWriteCode && refloat_package) {
            const auto& ld = refloat_package->lisp_data;
            size_t chunk = std::min(ld.size() - install_offset_, kUploadChunkSize);
            install_offset_ += chunk;
            if (install_offset_ >= ld.size()) {
                // Lisp upload complete → erase QML
                install_phase_ = InstallPhase::QmlErase;
                install_offset_ = 0;
            }
            send_install_command();
        }
        break;

    case InstallPhase::QmlErase:
        if (cmd == vesc::CommPacketID::QmluiErase) {
            install_phase_ = InstallPhase::QmlWrite;
            install_offset_ = 0;
            send_install_command();
        }
        break;

    case InstallPhase::QmlWrite:
        if (cmd == vesc::CommPacketID::QmluiWrite && refloat_package) {
            const auto& qd = refloat_package->qml_data;
            size_t chunk = std::min(qd.size() - install_offset_, kUploadChunkSize);
            install_offset_ += chunk;
            if (install_offset_ >= qd.size()) {
                install_phase_ = InstallPhase::SetRunning;
                install_offset_ = 0;
            }
            send_install_command();
        }
        break;

    case InstallPhase::SetRunning:
        if (cmd == vesc::CommPacketID::LispSetRunning) {
            install_phase_ = InstallPhase::Done;
            has_refloat = true;
            set_step(SetupStep::InstallRefloat, "Refloat installed");
            advance();
        }
        break;

    case InstallPhase::Done:
        break;
    }
}

} // namespace nosedive
