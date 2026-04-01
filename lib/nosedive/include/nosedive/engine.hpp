#pragma once

#include "nosedive/commands.hpp"
#include "nosedive/storage.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nosedive {

/// Callback to send a framed VESC packet payload to the transport.
using SendCallback = std::function<void(const uint8_t* payload, size_t len)>;

/// Callback when engine state changes and UI should refresh.
using StateCallback = std::function<void()>;

/// CAN device info discovered during enumeration.
struct CANDevice {
    uint8_t controller_id = 0;
    FWVersion fw;
};

/// The application engine. Owns all business logic and state.
/// Platform layer (Swift/Kotlin) feeds raw payloads in, reads state out.
class Engine {
public:
    explicit Engine(const std::string& storage_path);

    // --- Transport callbacks ---
    void set_send_callback(SendCallback cb) { send_cb_ = std::move(cb); }
    void set_state_callback(StateCallback cb) { state_cb_ = std::move(cb); }

    // --- Connection lifecycle ---
    void on_connected();
    void on_disconnected();

    // --- Payload handling (called by transport layer) ---
    void handle_payload(const uint8_t* data, size_t len);

    // --- Telemetry state (read by GUI) ---
    const Telemetry& telemetry() const { return telemetry_; }
    double speed_kmh() const { return telemetry_.speed * 3.6; }
    double speed_mph() const { return telemetry_.speed * 2.237; }

    // --- Active board ---
    const Board* active_board() const { return active_board_ ? &*active_board_ : nullptr; }
    bool is_known_board() const;
    const char* guessed_board_type() const;

    // --- CAN devices ---
    const std::vector<uint8_t>& can_device_ids() const { return can_device_ids_; }
    const std::vector<CANDevice>& can_devices() const { return can_devices_; }

    // --- FW info ---
    const FWVersion* main_fw() const { return main_fw_ ? &*main_fw_ : nullptr; }

    // --- Refloat ---
    bool has_refloat() const;
    const RefloatInfo* refloat_info() const { return refloat_info_ ? &*refloat_info_ : nullptr; }
    bool refloat_installing() const { return refloat_installing_; }
    bool refloat_installed() const { return refloat_installed_; }
    void install_refloat();

    // --- Wizard ---
    bool should_show_wizard() const { return show_wizard_; }
    void dismiss_wizard() { show_wizard_ = false; }

    // --- Board CRUD (delegates to storage) ---
    const std::vector<Board>& boards() const { return storage_.boards(); }
    void save_board(const Board& board);
    void remove_board(std::string_view id);

    // --- Profile CRUD (delegates to storage) ---
    const std::vector<RiderProfile>& profiles() const { return storage_.profiles(); }
    void save_profile(const RiderProfile& profile);
    void remove_profile(std::string_view id);
    const std::string& active_profile_id() const { return storage_.active_profile_id(); }
    void set_active_profile_id(const std::string& id);

    // --- Storage ---
    Storage& storage() { return storage_; }

private:
    Storage storage_;
    SendCallback send_cb_;
    StateCallback state_cb_;

    // Live state
    Telemetry telemetry_;
    std::optional<Board> active_board_;
    std::optional<FWVersion> main_fw_;
    std::optional<RefloatInfo> refloat_info_;

    // CAN discovery
    std::vector<uint8_t> can_device_ids_;
    std::vector<CANDevice> can_devices_;
    std::vector<uint8_t> pending_can_queries_;
    std::optional<uint8_t> awaiting_fw_from_;

    // Refloat install
    bool refloat_installing_ = false;
    bool refloat_installed_ = false;

    // Wizard
    bool show_wizard_ = false;

    // Board type guess result (cached)
    mutable std::string guessed_type_cache_;

    // --- Internal handlers ---
    void handle_values(const uint8_t* data, size_t len);
    void handle_fw_version(const uint8_t* data, size_t len);
    void handle_ping_can(const uint8_t* data, size_t len);
    void handle_custom_app_data(const uint8_t* data, size_t len);
    void handle_write_new_app_data();
    void handle_fw_info(const FWVersion& fw);
    void query_next_can_device();
    void send_payload(const std::vector<uint8_t>& payload);
    void notify_state_changed();
};

} // namespace nosedive
