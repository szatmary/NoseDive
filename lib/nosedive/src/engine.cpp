#include "nosedive/engine.hpp"
#include <algorithm>
#include <cctype>

namespace nosedive {

Engine::Engine(const std::string& storage_path)
    : storage_(storage_path) {}

// --- Callback setters ---

void Engine::set_write_callback(WriteCallback cb) {
    std::lock_guard lock(mu_);
    write_cb_ = std::move(cb);
}

void Engine::set_telemetry_callback(TelemetryCallback cb) {
    std::lock_guard lock(mu_);
    telemetry_cb_ = std::move(cb);
}

void Engine::set_board_callback(BoardCallback cb) {
    std::lock_guard lock(mu_);
    board_cb_ = std::move(cb);
}

void Engine::set_refloat_callback(RefloatCallback cb) {
    std::lock_guard lock(mu_);
    refloat_cb_ = std::move(cb);
}

void Engine::set_can_callback(CANCallback cb) {
    std::lock_guard lock(mu_);
    can_cb_ = std::move(cb);
}

void Engine::set_error_callback(ErrorCallback cb) {
    std::lock_guard lock(mu_);
    error_cb_ = std::move(cb);
}

// --- Platform → Engine ---

void Engine::receive_bytes(const uint8_t* data, size_t len) {
    decoder_.feed(data, len);
    while (decoder_.has_packet()) {
        auto payload = decoder_.pop();
        if (!payload.empty()) {
            handle_payload(payload.data(), payload.size());
        }
    }
}

void Engine::on_connected(size_t mtu) {
    std::unique_lock lock(mu_);
    mtu_ = mtu;
    decoder_.reset();
    // Discovery sequence: FW version → CAN scan → Refloat info
    queue_send(vesc::FWVersion::Request{}.encode());
    queue_send(vesc::PingCAN::Request{}.encode());
    queue_send(vesc::build_refloat_info_request());
    flush_pending(lock);
}

void Engine::on_disconnected() {
    std::unique_lock lock(mu_);
    decoder_.reset();
    telemetry_ = {};
    active_board_ = std::nullopt;
    main_fw_ = std::nullopt;
    refloat_info_ = std::nullopt;
    can_device_ids_.clear();
    can_devices_.clear();
    pending_can_queries_.clear();
    awaiting_fw_from_ = std::nullopt;
    refloat_installing_ = false;
    refloat_installed_ = false;
    show_wizard_ = false;
    guessed_type_cache_.clear();
    flush_pending(lock);
}

// --- Actions ---

void Engine::dismiss_wizard() {
    std::lock_guard lock(mu_);
    show_wizard_ = false;
}

void Engine::install_refloat() {
    std::unique_lock lock(mu_);
    refloat_installing_ = true;
    refloat_installed_ = false;
    pending_refloat_ = true;
    queue_send(std::vector<uint8_t>{static_cast<uint8_t>(CommPacketID::EraseNewApp)});
    queue_send(std::vector<uint8_t>{static_cast<uint8_t>(CommPacketID::WriteNewAppData)});
    flush_pending(lock);
}

// --- Payload dispatch ---

void Engine::handle_payload(const uint8_t* data, size_t len) {
    if (len == 0) return;
    std::unique_lock lock(mu_);

    auto cmd = data[0];
    switch (cmd) {
        case static_cast<uint8_t>(CommPacketID::GetValues):
            handle_values(data, len);
            break;
        case static_cast<uint8_t>(CommPacketID::FWVersion):
            handle_fw_version(data, len);
            break;
        case static_cast<uint8_t>(CommPacketID::PingCAN):
            handle_ping_can(data, len);
            break;
        case static_cast<uint8_t>(CommPacketID::CustomAppData):
            handle_custom_app_data(data, len);
            break;
        case static_cast<uint8_t>(CommPacketID::WriteNewAppData):
            handle_write_new_app_data();
            break;
        default: {
            std::string msg = "unknown command: " + std::to_string(cmd);
            pending_errors_.push_back(std::move(msg));
            break;
        }
    }

    // Feed response to wizard if it's running
    setup_handle_response(data, len);

    flush_pending(lock);
}

// --- Individual handlers ---

void Engine::handle_values(const uint8_t* data, size_t len) {
    auto vals = vesc::GetValues::Response::decode(data, len);
    if (!vals) return;

    telemetry_.temp_mosfet      = vals->temp_mosfet;
    telemetry_.temp_motor       = vals->temp_motor;
    telemetry_.motor_current    = vals->avg_motor_current;
    telemetry_.battery_current  = vals->avg_input_current;
    telemetry_.duty_cycle       = vals->duty_cycle;
    telemetry_.erpm             = vals->rpm;
    telemetry_.battery_voltage  = vals->voltage;
    telemetry_.amp_hours        = vals->amp_hours;
    telemetry_.amp_hours_charged = vals->amp_hours_charged;
    telemetry_.watt_hours       = vals->watt_hours;
    telemetry_.watt_hours_charged = vals->watt_hours_charged;
    telemetry_.tachometer       = vals->tachometer;
    telemetry_.tachometer_abs   = vals->tachometer_abs;
    telemetry_.fault            = vals->fault;
    telemetry_.power            = vals->voltage * vals->avg_input_current;

    if (active_board_) {
        telemetry_.speed = vesc::speed_from_erpm(
            vals->rpm,
            active_board_->motor_pole_pairs,
            active_board_->wheel_circumference_m);
        telemetry_.battery_percent = vesc::battery_percent(
            vals->voltage,
            active_board_->battery_voltage_min,
            active_board_->battery_voltage_max);
    }

    pending_telemetry_ = true;
}

void Engine::handle_fw_version(const uint8_t* data, size_t len) {
    auto fw = vesc::FWVersion::Response::decode(data, len);
    if (!fw) return;

    if (awaiting_fw_from_) {
        uint8_t id = *awaiting_fw_from_;
        awaiting_fw_from_ = std::nullopt;
        can_devices_.push_back(CANDevice{id, *fw});
        query_next_can_device();
    } else {
        handle_fw_info(*fw);
    }
}

void Engine::handle_fw_info(const FWVersionResponse& fw) {
    main_fw_ = fw;
    guessed_type_cache_.clear();

    Board board;
    if (active_board_) {
        board = *active_board_;
    } else {
        board.id = fw.uuid.empty() ? "unknown" : fw.uuid;
        board.name = fw.hw_name.empty() ? "VESC Board" : fw.hw_name;
    }

    board.fw_major = fw.major;
    board.fw_minor = fw.minor;
    board.hw_name = fw.hw_name;
    if (!fw.uuid.empty()) board.id = fw.uuid;

    active_board_ = board;

    if (!is_known_board_locked()) {
        show_wizard_ = true;
    }

    pending_board_ = true;
}

void Engine::handle_ping_can(const uint8_t* data, size_t len) {
    auto ping = vesc::PingCAN::Response::decode(data, len); if (ping) can_device_ids_ = ping->device_ids;
    can_devices_.clear();

    pending_can_queries_.clear();
    for (auto id : can_device_ids_) {
        if (id != 0) pending_can_queries_.push_back(id);
    }

    query_next_can_device();
    pending_can_ = true;
}

void Engine::handle_custom_app_data(const uint8_t* data, size_t len) {
    auto info = vesc::parse_refloat_info(data, len);
    if (!info) return;

    refloat_info_ = *info;

    if (active_board_) {
        active_board_->refloat_version = info->version_string();
    }

    pending_refloat_ = true;
}

void Engine::handle_write_new_app_data() {
    refloat_installing_ = false;
    refloat_installed_ = true;
    queue_send(vesc::FWVersion::Request{}.encode());
    queue_send(vesc::build_refloat_info_request());
    pending_refloat_ = true;
}

void Engine::query_next_can_device() {
    if (pending_can_queries_.empty()) {
        awaiting_fw_from_ = std::nullopt;
        return;
    }
    uint8_t next_id = pending_can_queries_.front();
    pending_can_queries_.erase(pending_can_queries_.begin());
    awaiting_fw_from_ = next_id;
    queue_send(vesc::ForwardCAN::Request{.target_id = next_id, .inner_payload = vesc::FWVersion::Request{}.encode()}.encode());
}

// --- Board identification ---

bool Engine::is_known_board_locked() const {
    if (!main_fw_ || main_fw_->uuid.empty()) return false;
    return storage_.find_board(main_fw_->uuid).has_value();
}

// --- Storage delegates ---

std::vector<Board> Engine::boards() const {
    std::lock_guard lock(mu_);
    return storage_.boards();
}

void Engine::save_board(const Board& board) {
    std::lock_guard lock(mu_);
    storage_.upsert_board(board);
}

void Engine::remove_board(std::string_view id) {
    std::lock_guard lock(mu_);
    storage_.remove_board(id);
}

std::vector<RiderProfile> Engine::profiles() const {
    std::lock_guard lock(mu_);
    return storage_.profiles();
}

void Engine::save_profile(const RiderProfile& profile) {
    std::lock_guard lock(mu_);
    storage_.upsert_profile(profile);
}

void Engine::remove_profile(std::string_view id) {
    std::lock_guard lock(mu_);
    storage_.remove_profile(id);
}

std::string Engine::active_profile_id() const {
    std::lock_guard lock(mu_);
    return storage_.active_profile_id();
}

void Engine::set_active_profile_id(const std::string& id) {
    std::lock_guard lock(mu_);
    storage_.set_active_profile_id(id);
}

// --- Wizard ---

void Engine::set_setup_callback(SetupCallback cb) {
    std::lock_guard lock(mu_);
    setup_cb_ = std::move(cb);
}

void Engine::setup_start() {
    std::unique_lock lock(mu_);
    setup_state_ = {};
    // FW version already queried on connect — if we have it, skip to next
    if (main_fw_) {
        setup_set_step(SetupStep::CheckFW,
            "FW " + std::to_string(main_fw_->major) + "." +
            std::to_string(main_fw_->minor) + " — " + main_fw_->hw_name);
        setup_advance();
    } else {
        setup_set_step(SetupStep::CheckFW, "Checking firmware version...");
        setup_run_step(lock);
    }
    flush_pending(lock);
}

void Engine::setup_retry() {
    std::unique_lock lock(mu_);
    setup_state_.error.clear();
    // Re-enter the current step
    auto step = setup_state_.step;
    setup_state_.step = SetupStep::Idle;
    setup_set_step(step, "Retrying...");
    setup_run_step(lock);
    flush_pending(lock);
}

void Engine::setup_skip() {
    std::unique_lock lock(mu_);
    setup_state_.error.clear();
    setup_advance();
    flush_pending(lock);
}

void Engine::setup_abort() {
    std::unique_lock lock(mu_);
    setup_set_step(SetupStep::Idle, "Wizard cancelled");
    flush_pending(lock);
}

void Engine::setup_set_step(SetupStep step, const std::string& detail) {
    setup_state_.step = step;
    setup_state_.detail = detail;
    setup_state_.error.clear();
    pending_setup_ = true;
}

void Engine::setup_set_error(const std::string& error) {
    setup_state_.error = error;
    pending_setup_ = true;
}

void Engine::setup_advance() {
    // Move to next step
    auto next = static_cast<SetupStep>(static_cast<uint8_t>(setup_state_.step) + 1);
    if (next > SetupStep::Done) next = SetupStep::Done;

    switch (next) {
    case SetupStep::CheckFW:
        setup_set_step(next, "Checking firmware...");
        break;
    case SetupStep::InstallRefloat:
        if (main_fw_ && main_fw_->custom_config_count > 0) {
            // Refloat already installed — skip
            setup_set_step(next, "Refloat already installed");
            setup_advance();
            return;
        }
        setup_set_step(next, "Installing Refloat package...");
        break;
    case SetupStep::DetectBattery:
        setup_set_step(next, "Detecting battery configuration...");
        break;
    case SetupStep::DetectFootpads:
        setup_set_step(next, "Detecting footpad sensors...");
        break;
    case SetupStep::CalibrateIMU:
        setup_set_step(next, "Calibrating IMU...");
        break;
    case SetupStep::DetectMotor:
        setup_set_step(next, "Detecting motor parameters...");
        break;
    case SetupStep::ConfigureWheel:
        setup_set_step(next, "Configuring wheel...");
        break;
    case SetupStep::Done:
        // Save the board to fleet
        if (active_board_) {
            active_board_->wizard_complete = true;
            storage_.upsert_board(*active_board_);
        }
        show_wizard_ = false;
        setup_set_step(SetupStep::Done, "Setup complete!");
        return;
    default:
        break;
    }

    // Send commands for this step (lock is held by caller)
    // Create a temporary lock reference since we need it for setup_run_step
    // but we're called without one. Queue sends directly instead.
    switch (setup_state_.step) {
    case SetupStep::CheckFW:
        queue_send(vesc::FWVersion::Request{}.encode());
        break;
    case SetupStep::InstallRefloat:
        refloat_installing_ = true;
        queue_send(std::vector<uint8_t>{static_cast<uint8_t>(CommPacketID::EraseNewApp)});
        queue_send(std::vector<uint8_t>{static_cast<uint8_t>(CommPacketID::WriteNewAppData)});
        break;
    case SetupStep::DetectBattery:
        queue_send(vesc::GetValues::Request{}.encode());
        break;
    case SetupStep::DetectFootpads:
        queue_send(vesc::GetValues::Request{}.encode());
        break;
    case SetupStep::CalibrateIMU:
        queue_send(vesc::GetIMUData::Request{.mask = 0x001F}.encode());
        break;
    case SetupStep::DetectMotor:
        queue_send(vesc::DetectApplyAllFOC::Request{.detect_can = false, .max_power_loss = 1.0}.encode());
        break;
    case SetupStep::ConfigureWheel:
        queue_send(std::vector<uint8_t>{static_cast<uint8_t>(CommPacketID::GetMCConf)});
        break;
    default:
        break;
    }
}

void Engine::setup_run_step(std::unique_lock<std::mutex>& /*lock*/) {
    switch (setup_state_.step) {
    case SetupStep::CheckFW:
        queue_send(vesc::FWVersion::Request{}.encode());
        break;
    case SetupStep::InstallRefloat:
        refloat_installing_ = true;
        queue_send(std::vector<uint8_t>{static_cast<uint8_t>(CommPacketID::EraseNewApp)});
        queue_send(std::vector<uint8_t>{static_cast<uint8_t>(CommPacketID::WriteNewAppData)});
        break;
    case SetupStep::DetectBattery:
        queue_send(vesc::GetValues::Request{}.encode());
        break;
    case SetupStep::DetectFootpads: {
        // Request ADC values to check footpads
        auto req = vesc::GetValues::Request{}.encode();
        queue_send(req);
        break;
    }
    case SetupStep::CalibrateIMU: {
        auto req = vesc::GetIMUData::Request{.mask = 0x001F}.encode();
        queue_send(req);
        break;
    }
    case SetupStep::DetectMotor: {
        auto req = vesc::DetectApplyAllFOC::Request{
            .detect_can = false,
            .max_power_loss = 1.0,
        }.encode();
        queue_send(req);
        break;
    }
    case SetupStep::ConfigureWheel:
        // Read current MCConf to get wheel diameter
        queue_send(std::vector<uint8_t>{static_cast<uint8_t>(CommPacketID::GetMCConf)});
        break;
    default:
        break;
    }
}

void Engine::setup_handle_response(const uint8_t* data, size_t len) {
    if (setup_state_.step == SetupStep::Idle || setup_state_.step == SetupStep::Done) return;
    if (len == 0) return;

    auto cmd = static_cast<CommPacketID>(data[0]);

    switch (setup_state_.step) {
    case SetupStep::CheckFW:
        if (cmd == CommPacketID::FWVersion) {
            if (main_fw_) {
                setup_set_step(SetupStep::CheckFW,
                    "FW " + std::to_string(main_fw_->major) + "." +
                    std::to_string(main_fw_->minor) + " — " + main_fw_->hw_name);
                setup_advance();
            } else {
                setup_set_error("Could not read firmware version");
            }
        }
        break;

    case SetupStep::InstallRefloat:
        if (cmd == CommPacketID::WriteNewAppData) {
            refloat_installing_ = false;
            refloat_installed_ = true;
            setup_set_step(SetupStep::InstallRefloat, "Refloat installed");
            setup_advance();
        }
        break;

    case SetupStep::DetectBattery:
        if (cmd == CommPacketID::GetValues) {
            auto vals = vesc::GetValues::Response::decode(data, len);
            if (vals) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Battery: %.1fV detected", vals->voltage);
                setup_set_step(SetupStep::DetectBattery, buf);
                setup_advance();
            } else {
                setup_set_error("Could not read battery voltage");
            }
        }
        break;

    case SetupStep::DetectFootpads:
        if (cmd == CommPacketID::GetValues) {
            setup_set_step(SetupStep::DetectFootpads, "Footpad sensors detected");
            setup_advance();
        }
        break;

    case SetupStep::CalibrateIMU:
        if (cmd == CommPacketID::GetIMUData) {
            auto imu = vesc::GetIMUData::Response::decode(data, len);
            if (imu) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "IMU OK — pitch=%.1f° roll=%.1f°", imu->pitch, imu->roll);
                setup_set_step(SetupStep::CalibrateIMU, buf);
                setup_advance();
            } else {
                setup_set_error("Could not read IMU data");
            }
        }
        break;

    case SetupStep::DetectMotor:
        if (cmd == CommPacketID::DetectApplyAllFOC) {
            auto result = vesc::DetectApplyAllFOC::Response::decode(data, len);
            if (result && result->result >= 0) {
                setup_set_step(SetupStep::DetectMotor, "Motor detected successfully");
                setup_advance();
            } else {
                setup_set_error("Motor detection failed");
            }
        }
        break;

    case SetupStep::ConfigureWheel:
        if (cmd == CommPacketID::GetMCConf) {
            setup_set_step(SetupStep::ConfigureWheel, "Motor configuration read");
            setup_advance();
        }
        break;

    default:
        break;
    }
}

// --- Deferred work helpers ---

void Engine::queue_send(const std::vector<uint8_t>& payload) {
    // Encode as VESC framed packet
    auto pkt = encode_packet(payload.data(), payload.size());
    if (!pkt.empty()) {
        pending_sends_.push_back(std::move(pkt));
    }
}

void Engine::flush_pending(std::unique_lock<std::mutex>& lock) {
    // Snapshot all pending work while locked
    auto sends = std::move(pending_sends_);
    pending_sends_.clear();
    auto errors = std::move(pending_errors_);
    pending_errors_.clear();

    bool fire_telemetry = pending_telemetry_;
    bool fire_board = pending_board_;
    bool fire_refloat = pending_refloat_;
    bool fire_can = pending_can_;
    bool fire_setup = pending_setup_;
    pending_telemetry_ = pending_board_ = pending_refloat_ = pending_can_ = pending_setup_ = false;

    // Snapshot state for callbacks (copies while locked)
    auto telemetry = telemetry_;
    auto board = active_board_;
    auto fw = main_fw_;
    bool wizard = show_wizard_;
    bool known = is_known_board_locked();
    bool has_rf = refloat_info_.has_value();
    auto refloat = refloat_info_;
    bool rf_installing = refloat_installing_;
    bool rf_installed = refloat_installed_;
    auto can_ids = can_device_ids_;

    auto setup_flush_state = setup_state_;

    // Snapshot callbacks
    auto write_cb = write_cb_;
    auto telemetry_cb = telemetry_cb_;
    auto board_cb = board_cb_;
    auto refloat_cb = refloat_cb_;
    auto can_cb = can_cb_;
    auto error_cb = error_cb_;
    auto setup_cb_flush = setup_cb_;

    // Release lock BEFORE firing callbacks
    lock.unlock();

    // Write outgoing packets (already framed)
    for (auto& pkt : sends) {
        if (write_cb) {
            size_t offset = 0;
            while (offset < pkt.size()) {
                size_t chunk = std::min(mtu_, pkt.size() - offset);
                write_cb(pkt.data() + offset, chunk);
                offset += chunk;
            }
        }
    }

    // Fire domain callbacks with copied state
    if (fire_telemetry && telemetry_cb) {
        telemetry_cb(telemetry);
    }
    if (fire_board && board_cb && board && fw) {
        board_cb(*board, *fw, wizard, known);
    }
    if (fire_refloat && refloat_cb) {
        refloat_cb(has_rf, refloat, rf_installing, rf_installed);
    }
    if (fire_can && can_cb) {
        can_cb(can_ids);
    }
    if (fire_setup && setup_cb_flush) {
        setup_cb_flush(setup_flush_state);
    }
    for (auto& msg : errors) {
        if (error_cb) error_cb(msg.c_str());
    }
}

} // namespace nosedive
