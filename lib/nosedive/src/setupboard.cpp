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
    case SetupStep::FWExpress: return UpdateTarget::Express;
    case SetupStep::FWBMS:     return UpdateTarget::BMS;
    case SetupStep::FWVESC:    return UpdateTarget::VESC;
    default:                   return UpdateTarget::None;
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

// Check FW version, set state with result. Returns true if outdated.
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
        set_state(step, StepPhase::Prompt, buf);
    } else {
        std::snprintf(buf, sizeof(buf), "%s FW %d.%02d — %s — up to date",
            label_for(target), fw->major, fw->minor, fw->hw_name.c_str());
        set_state(step, StepPhase::Working, buf);
    }
    return outdated;
}

// Start checking firmware for a step.
void SetupBoard::begin_fw_check(SetupStep step) {
    auto target = target_for_step(step);
    auto& fw = fw_for(target);
    if (fw) {
        // Already have FW info — report and auto-advance if up to date
        if (!check_and_report_fw(step, target)) {
            advance();
        }
    } else {
        set_state(step, StepPhase::Working,
            std::string("Checking ") + label_for(target) + " firmware...");
        send_fw_query(target);
    }
}

// Handle FW version response during a FW check step (phase == Working).
void SetupBoard::handle_fw_response(const uint8_t* data, size_t len) {
    auto fw = vesc::FWVersion::Response::decode(data, len);
    auto target = target_for_step(state_.step);
    if (!fw) {
        set_error(std::string("Could not read ") + label_for(target) + " firmware");
        return;
    }
    fw_for(target) = *fw;
    if (!check_and_report_fw(state_.step, target)) {
        advance(); // Up to date — auto-advance
    }
    // If outdated, now at Prompt — user decides update/skip
}

// Handle WriteNewAppData ack during firmware update (phase == Working after update()).
void SetupBoard::handle_fw_update_response(vesc::CommPacketID cmd) {
    if (cmd == vesc::CommPacketID::WriteNewAppData) {
        set_state(state_.step, StepPhase::WaitReconnect,
            "Board is rebooting, waiting for reconnect...");
    }
}

// Common logic for advancing through the FW check chain.
void SetupBoard::advance_to_next_fw_check() {
    auto current = state_.step;

    if (current == SetupStep::FWExpress) {
        express_checked_ = true;
        if (find_can_id(UpdateTarget::BMS)) {
            begin_fw_check(SetupStep::FWBMS);
            return;
        }
        bms_checked_ = true;
    } else if (current == SetupStep::FWBMS) {
        bms_checked_ = true;
    }

    // Fall through to VESC check
    begin_fw_check(SetupStep::FWVESC);
}

// --- Public API ---

void SetupBoard::start() {
    state_ = {};
    express_checked_ = false;
    bms_checked_ = false;
    express_fw_ = std::nullopt;
    bms_fw_ = std::nullopt;

    if (find_can_id(UpdateTarget::Express)) {
        begin_fw_check(SetupStep::FWExpress);
    } else if (find_can_id(UpdateTarget::BMS)) {
        express_checked_ = true;
        begin_fw_check(SetupStep::FWBMS);
    } else {
        express_checked_ = true;
        bms_checked_ = true;
        begin_fw_check(SetupStep::FWVESC);
    }
}

void SetupBoard::retry() {
    state_.error.clear();
    // Re-enter the current step's working phase
    auto target = target_for_step(state_.step);
    if (target != UpdateTarget::None) {
        set_state(state_.step, StepPhase::Working,
            std::string("Checking ") + label_for(target) + " firmware...");
        send_fw_query(target);
    } else if (state_.step == SetupStep::FactoryReset) {
        set_state(SetupStep::FactoryReset, StepPhase::Working,
            "Resetting motor configuration...");
        reset_phase_ = ResetPhase::GetMCDefault;
        reset_conf_blob_.clear();
        send_reset_command();
    } else if (state_.step == SetupStep::InstallRefloat) {
        // Retry install from the beginning
        set_state(SetupStep::InstallRefloat, StepPhase::Working,
            "Installing Refloat package...");
        install_phase_ = InstallPhase::LispErase;
        install_offset_ = 0;
        send_install_command();
    }
    fire_callback();
}

void SetupBoard::skip() {
    // Cannot skip fresh Refloat install (no existing version)
    if (state_.step == SetupStep::InstallRefloat && !refloat_info) return;
    // FW steps, FactoryReset, and InstallRefloat require Prompt phase to skip
    if (state_.phase != StepPhase::Prompt) {
        auto target = target_for_step(state_.step);
        if (target != UpdateTarget::None) return; // FW step not at Prompt
        if (state_.step == SetupStep::FactoryReset) return;
        if (state_.step == SetupStep::InstallRefloat) return;
    }
    state_.error.clear();
    advance();
}

void SetupBoard::update() {
    if (state_.phase != StepPhase::Prompt) return;

    // Factory reset
    if (state_.step == SetupStep::FactoryReset) {
        set_state(SetupStep::FactoryReset, StepPhase::Working,
            "Resetting motor configuration...");
        reset_phase_ = ResetPhase::GetMCDefault;
        reset_conf_blob_.clear();
        send_reset_command();
        return;
    }

    // Refloat install/update
    if (state_.step == SetupStep::InstallRefloat) {
        set_state(SetupStep::InstallRefloat, StepPhase::Working,
            "Installing Refloat package...");
        install_phase_ = InstallPhase::LispErase;
        install_offset_ = 0;
        send_install_command();
        return;
    }

    // Firmware update
    auto target = target_for_step(state_.step);
    if (target == UpdateTarget::None) return;

    char buf[128];
    std::snprintf(buf, sizeof(buf), "Uploading %s firmware...", label_for(target));
    set_state(state_.step, StepPhase::Working, buf);
    send_fw_update(target);
}

void SetupBoard::on_reconnected() {
    if (state_.phase != StepPhase::WaitReconnect) return;

    auto target = target_for_step(state_.step);
    set_state(state_.step, StepPhase::Working, "Reconnected, verifying firmware...");
    send_fw_query(target);
}

void SetupBoard::abort() {
    set_state(SetupStep::Idle, StepPhase::Working, "Setup cancelled");
}

// --- Internal helpers ---

void SetupBoard::set_state(SetupStep step, StepPhase phase, const std::string& detail) {
    state_.step = step;
    state_.phase = phase;
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
    case SetupStep::FWExpress:
    case SetupStep::FWBMS:
        advance_to_next_fw_check();
        return;

    case SetupStep::FWVESC:
        set_state(SetupStep::FactoryReset, StepPhase::Prompt,
            "Reset all settings to factory defaults?");
        return;

    case SetupStep::FactoryReset: {
        char buf[256];
        if (refloat_info) {
            bool outdated = LatestRefloat::is_outdated(
                refloat_info->major, refloat_info->minor, refloat_info->patch);
            if (outdated) {
                std::snprintf(buf, sizeof(buf),
                    "Refloat %d.%d.%d — update available (%d.%d.%d)",
                    refloat_info->major, refloat_info->minor, refloat_info->patch,
                    LatestRefloat::major, LatestRefloat::minor, LatestRefloat::patch);
            } else {
                std::snprintf(buf, sizeof(buf), "Refloat %d.%d.%d — up to date",
                    refloat_info->major, refloat_info->minor, refloat_info->patch);
            }
        } else {
            std::snprintf(buf, sizeof(buf),
                "Refloat not installed — install %d.%d.%d?",
                LatestRefloat::major, LatestRefloat::minor, LatestRefloat::patch);
        }
        set_state(SetupStep::InstallRefloat, StepPhase::Prompt, buf);
        return;
    }

    case SetupStep::InstallRefloat:
        set_state(SetupStep::DetectFootpads, StepPhase::Working, "Detecting footpad sensors...");
        break;

    case SetupStep::DetectFootpads:
        set_state(SetupStep::CalibrateIMU, StepPhase::Working, "Calibrating IMU...");
        break;

    case SetupStep::CalibrateIMU:
        set_state(SetupStep::DetectMotor, StepPhase::Working, "Detecting motor parameters...");
        break;

    case SetupStep::DetectMotor:
        set_state(SetupStep::ConfigureWheel, StepPhase::Working, "Configuring wheel...");
        break;

    case SetupStep::ConfigureWheel:
        set_state(SetupStep::ConfigurePower, StepPhase::Working, "Configuring power...");
        break;

    case SetupStep::ConfigurePower:
        set_state(SetupStep::Done, StepPhase::Working, "Setup complete!");
        return;

    default:
        break;
    }

    // Send commands for the new working step
    switch (state_.step) {
    case SetupStep::DetectFootpads:
        if (send_cb_) send_cb_(vesc::GetValues::Request{}.encode());
        break;
    case SetupStep::CalibrateIMU:
        if (send_cb_) send_cb_(vesc::GetIMUData::Request{.mask = 0x001F}.encode());
        break;
    case SetupStep::DetectMotor:
        if (send_cb_) send_cb_(vesc::DetectApplyAllFOC::Request{
            .detect_can = false, .max_power_loss = 1.0}.encode());
        break;
    case SetupStep::ConfigureWheel:
        if (send_cb_) send_cb_({static_cast<uint8_t>(vesc::CommPacketID::GetMCConf)});
        break;
    case SetupStep::ConfigurePower:
        if (send_cb_) {
            // If BMS is on CAN, query it for cell-level data
            if (auto bms_id = find_can_id(UpdateTarget::BMS)) {
                send_cb_(vesc::ForwardCAN::Request{
                    .target_id = *bms_id,
                    .inner_payload = vesc::BMSGetValues::Request{}.encode()
                }.encode());
            } else {
                // No BMS — just read pack voltage from VESC
                send_cb_(vesc::GetValues::Request{}.encode());
            }
        }
        break;
    default:
        break;
    }
}

// --- Response dispatch ---

void SetupBoard::handle_response(const uint8_t* data, size_t len) {
    if (!is_running() || len == 0) return;

    auto cmd = static_cast<vesc::CommPacketID>(data[0]);

    switch (state_.step) {
    // --- Firmware steps ---
    case SetupStep::FWExpress:
    case SetupStep::FWBMS:
    case SetupStep::FWVESC:
        if (state_.phase == StepPhase::Working) {
            if (cmd == vesc::CommPacketID::FWVersion) {
                handle_fw_response(data, len);
            } else if (cmd == vesc::CommPacketID::WriteNewAppData) {
                handle_fw_update_response(cmd);
            }
        } else if (state_.phase == StepPhase::WaitReconnect) {
            if (cmd == vesc::CommPacketID::FWVersion) {
                auto fw = vesc::FWVersion::Response::decode(data, len);
                if (!fw) {
                    set_error("Could not verify firmware after update");
                    break;
                }
                auto target = target_for_step(state_.step);
                fw_for(target) = *fw;
                char buf[256];
                std::snprintf(buf, sizeof(buf), "%s FW updated to %d.%02d",
                    label_for(target), fw->major, fw->minor);
                set_state(state_.step, StepPhase::Prompt, buf);
                // Now at Prompt — user skips to continue to next step
                advance();
            }
        }
        break;

    // --- Factory reset ---
    case SetupStep::FactoryReset:
        if (state_.phase == StepPhase::Working) {
            handle_reset_response(cmd, data, len);
        }
        break;

    // --- Refloat install ---
    case SetupStep::InstallRefloat:
        if (state_.phase == StepPhase::Working) {
            handle_install_response(cmd, data, len);
        }
        break;

    // --- Detection steps ---
    case SetupStep::DetectFootpads:
        if (cmd == vesc::CommPacketID::GetValues) {
            set_state(SetupStep::DetectFootpads, StepPhase::Working, "Footpad sensors OK");
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
            set_state(SetupStep::CalibrateIMU, StepPhase::Working, buf);
            advance();
        }
        break;

    case SetupStep::DetectMotor:
        if (cmd == vesc::CommPacketID::DetectApplyAllFOC) {
            auto r = vesc::DetectApplyAllFOC::Response::decode(data, len);
            if (!r) { set_error("Motor detection failed"); break; }
            if (r->result >= 0) {
                set_state(SetupStep::DetectMotor, StepPhase::Working, "Motor detected successfully");
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
            set_state(SetupStep::ConfigureWheel, StepPhase::Working, "Configuration read");
            advance();
        }
        break;

    case SetupStep::ConfigurePower:
        if (cmd == vesc::CommPacketID::BMSGetValues) {
            auto bms = vesc::BMSGetValues::Response::decode(data, len);
            if (!bms) { set_error("Could not read BMS data"); break; }
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "BMS: %.1fV, %dS, SoC %.0f%%",
                bms->voltage, bms->cell_count, bms->soc * 100.0);
            set_state(SetupStep::ConfigurePower, StepPhase::Working, buf);
            advance();
        } else if (cmd == vesc::CommPacketID::GetValues) {
            auto v = vesc::GetValues::Response::decode(data, len);
            if (!v) { set_error("Could not read battery voltage"); break; }
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Battery: %.1fV", v->voltage);
            set_state(SetupStep::ConfigurePower, StepPhase::Working, buf);
            advance();
        }
        break;

    default:
        break;
    }
}

// --- Factory reset sub-state machine ---

void SetupBoard::send_reset_command() {
    if (!send_cb_) return;

    switch (reset_phase_) {
    case ResetPhase::GetMCDefault:
        set_state(SetupStep::FactoryReset, StepPhase::Working,
            "Resetting motor configuration...");
        send_cb_({static_cast<uint8_t>(vesc::CommPacketID::GetMCConfDefault)});
        break;

    case ResetPhase::SetMC:
        // Send the default MC config blob back with SetMCConf command
        {
            std::vector<uint8_t> pkt;
            pkt.reserve(1 + reset_conf_blob_.size());
            pkt.push_back(static_cast<uint8_t>(vesc::CommPacketID::SetMCConf));
            pkt.insert(pkt.end(), reset_conf_blob_.begin(), reset_conf_blob_.end());
            send_cb_(pkt);
        }
        break;

    case ResetPhase::GetAppDefault:
        set_state(SetupStep::FactoryReset, StepPhase::Working,
            "Resetting app configuration...");
        send_cb_({static_cast<uint8_t>(vesc::CommPacketID::GetAppConfDefault)});
        break;

    case ResetPhase::SetApp:
        // Send the default App config blob back with SetAppConf command
        {
            std::vector<uint8_t> pkt;
            pkt.reserve(1 + reset_conf_blob_.size());
            pkt.push_back(static_cast<uint8_t>(vesc::CommPacketID::SetAppConf));
            pkt.insert(pkt.end(), reset_conf_blob_.begin(), reset_conf_blob_.end());
            send_cb_(pkt);
        }
        break;

    case ResetPhase::Done:
        break;
    }
}

void SetupBoard::handle_reset_response(vesc::CommPacketID cmd,
                                        const uint8_t* data, size_t len) {
    switch (reset_phase_) {
    case ResetPhase::GetMCDefault:
        if (cmd == vesc::CommPacketID::GetMCConfDefault) {
            // Store the config blob (everything after the command byte)
            reset_conf_blob_.assign(data + 1, data + len);
            reset_phase_ = ResetPhase::SetMC;
            send_reset_command();
        }
        break;

    case ResetPhase::SetMC:
        if (cmd == vesc::CommPacketID::SetMCConf) {
            reset_conf_blob_.clear();
            reset_phase_ = ResetPhase::GetAppDefault;
            send_reset_command();
        }
        break;

    case ResetPhase::GetAppDefault:
        if (cmd == vesc::CommPacketID::GetAppConfDefault) {
            reset_conf_blob_.assign(data + 1, data + len);
            reset_phase_ = ResetPhase::SetApp;
            send_reset_command();
        }
        break;

    case ResetPhase::SetApp:
        if (cmd == vesc::CommPacketID::SetAppConf) {
            reset_phase_ = ResetPhase::Done;
            reset_conf_blob_.clear();
            set_state(SetupStep::FactoryReset, StepPhase::Working,
                "Factory reset complete");
            advance();
        }
        break;

    case ResetPhase::Done:
        break;
    }
}

// --- Refloat install sub-state machine ---

void SetupBoard::send_install_command() {
    if (!send_cb_) return;

    switch (install_phase_) {
    case InstallPhase::LispErase:
        set_state(SetupStep::InstallRefloat, StepPhase::Working, "Erasing Lisp code...");
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
        set_state(SetupStep::InstallRefloat, StepPhase::Working, buf);

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
        set_state(SetupStep::InstallRefloat, StepPhase::Working, "Erasing QML UI...");
        send_cb_({static_cast<uint8_t>(vesc::CommPacketID::QmluiErase)});
        break;

    case InstallPhase::QmlWrite: {
        if (!refloat_package || refloat_package->qml_data.empty()) {
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
        set_state(SetupStep::InstallRefloat, StepPhase::Working, buf);

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
        set_state(SetupStep::InstallRefloat, StepPhase::Working, "Starting Lisp runtime...");
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
            refloat_info = vesc::RefloatInfo{
                "Refloat", LatestRefloat::major, LatestRefloat::minor,
                LatestRefloat::patch, ""};
            set_state(SetupStep::InstallRefloat, StepPhase::Working, "Refloat installed");
            advance();
        }
        break;

    case InstallPhase::Done:
        break;
    }
}

} // namespace nosedive
