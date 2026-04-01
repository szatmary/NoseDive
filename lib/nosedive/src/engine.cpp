#include "nosedive/engine.hpp"
#include "nosedive/protocol.hpp"
#include <algorithm>
#include <cctype>

namespace nosedive {

Engine::Engine(const std::string& storage_path)
    : storage_(storage_path) {}

// --- Connection lifecycle ---

void Engine::on_connected() {
    // Discovery sequence: FW version → CAN scan → Refloat info
    send_payload(build_command(CommPacketID::FWVersion));
    send_payload(build_command(CommPacketID::PingCAN));
    send_payload(build_refloat_info_request());
}

void Engine::on_disconnected() {
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
    notify_state_changed();
}

// --- Payload dispatch ---

void Engine::handle_payload(const uint8_t* data, size_t len) {
    if (len == 0) return;

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
        default:
            break;
    }
}

// --- Individual handlers ---

void Engine::handle_values(const uint8_t* data, size_t len) {
    auto vals = parse_values(data, len);
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

    // Compute speed and battery % if we have a board
    if (active_board_) {
        telemetry_.speed = speed_from_erpm(
            vals->rpm,
            active_board_->motor_pole_pairs,
            active_board_->wheel_circumference_m);
        telemetry_.battery_percent = battery_percent(
            vals->voltage,
            active_board_->battery_voltage_min,
            active_board_->battery_voltage_max);
    }

    notify_state_changed();
}

void Engine::handle_fw_version(const uint8_t* data, size_t len) {
    auto fw = parse_fw_version(data, len);
    if (!fw) return;

    if (awaiting_fw_from_) {
        // This is a CAN device response
        uint8_t id = *awaiting_fw_from_;
        awaiting_fw_from_ = std::nullopt;

        can_devices_.push_back(CANDevice{id, *fw});
        query_next_can_device();
    } else {
        // Main VESC response
        handle_fw_info(*fw);
    }

    notify_state_changed();
}

void Engine::handle_fw_info(const FWVersion& fw) {
    main_fw_ = fw;
    guessed_type_cache_.clear(); // invalidate cache

    // Create or update active board
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

    // Check if this board is in the fleet
    if (!is_known_board()) {
        show_wizard_ = true;
    }
}

void Engine::handle_ping_can(const uint8_t* data, size_t len) {
    can_device_ids_ = parse_ping_can(data, len);
    can_devices_.clear();

    // Queue non-zero IDs for sequential FW queries
    pending_can_queries_.clear();
    for (auto id : can_device_ids_) {
        if (id != 0) pending_can_queries_.push_back(id);
    }

    query_next_can_device();
    notify_state_changed();
}

void Engine::handle_custom_app_data(const uint8_t* data, size_t len) {
    auto info = parse_refloat_info(data, len);
    if (!info) return;

    refloat_info_ = *info;

    // Update active board with refloat version
    if (active_board_) {
        active_board_->refloat_version = info->version_string();
    }

    notify_state_changed();
}

void Engine::handle_write_new_app_data() {
    // Refloat install complete — re-query
    refloat_installing_ = false;
    refloat_installed_ = true;
    send_payload(build_command(CommPacketID::FWVersion));
    send_payload(build_refloat_info_request());
    notify_state_changed();
}

void Engine::query_next_can_device() {
    if (pending_can_queries_.empty()) {
        awaiting_fw_from_ = std::nullopt;
        return;
    }
    uint8_t next_id = pending_can_queries_.front();
    pending_can_queries_.erase(pending_can_queries_.begin());
    awaiting_fw_from_ = next_id;
    send_payload(build_fw_version_request_can(next_id));
}

// --- Refloat install ---

void Engine::install_refloat() {
    refloat_installing_ = true;
    refloat_installed_ = false;
    send_payload(build_command(CommPacketID::EraseNewApp));
    send_payload(build_command(CommPacketID::WriteNewAppData));
    notify_state_changed();
}

// --- Board identification ---

bool Engine::is_known_board() const {
    if (!main_fw_ || main_fw_->uuid.empty()) return false;
    return storage_.find_board(main_fw_->uuid).has_value();
}

const char* Engine::guessed_board_type() const {
    if (!guessed_type_cache_.empty()) return guessed_type_cache_.c_str();
    if (!main_fw_) return nullptr;

    std::string hw = main_fw_->hw_name;
    std::transform(hw.begin(), hw.end(), hw.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (hw.find("thor") != std::string::npos) { guessed_type_cache_ = "Funwheel X7"; }
    else if (hw.find("ubox") != std::string::npos) { guessed_type_cache_ = "DIY Ubox Build"; }
    else if (hw.find("little focer") != std::string::npos) { guessed_type_cache_ = "XR VESC Conversion"; }
    else if (hw.find("75/300") != std::string::npos || hw.find("100/250") != std::string::npos) { guessed_type_cache_ = "Trampa VESC Build"; }
    else if (hw.find("mk6") != std::string::npos || hw.find("mk5") != std::string::npos || hw.find("mk4") != std::string::npos) { guessed_type_cache_ = "VESC Build"; }

    return guessed_type_cache_.empty() ? nullptr : guessed_type_cache_.c_str();
}

bool Engine::has_refloat() const {
    return main_fw_ && main_fw_->custom_config_count > 0;
}

// --- Storage delegates ---

void Engine::save_board(const Board& board) {
    storage_.upsert_board(board);
}

void Engine::remove_board(std::string_view id) {
    storage_.remove_board(id);
}

void Engine::save_profile(const RiderProfile& profile) {
    storage_.upsert_profile(profile);
}

void Engine::remove_profile(std::string_view id) {
    storage_.remove_profile(id);
}

void Engine::set_active_profile_id(const std::string& id) {
    storage_.set_active_profile_id(id);
}

// --- Helpers ---

void Engine::send_payload(const std::vector<uint8_t>& payload) {
    if (send_cb_) {
        send_cb_(payload.data(), payload.size());
    }
}

void Engine::notify_state_changed() {
    if (state_cb_) {
        state_cb_();
    }
}

} // namespace nosedive
