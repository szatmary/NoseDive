#pragma once

#include "nosedive/commands.hpp"
#include "nosedive/storage.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nosedive {

/// Callback to send a framed VESC packet payload to the transport.
using SendCallback = std::function<void(const uint8_t* payload, size_t len)>;

/// Callback when engine state changes and UI should refresh.
using StateCallback = std::function<void()>;

/// Callback for diagnostic messages (unknown commands, parse failures).
using ErrorCallback = std::function<void(const char* message)>;

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
    void set_send_callback(SendCallback cb);
    void set_state_callback(StateCallback cb);
    void set_error_callback(ErrorCallback cb);

    // --- Connection lifecycle ---
    void on_connected();
    void on_disconnected();

    // --- Payload handling (called by transport layer) ---
    void handle_payload(const uint8_t* data, size_t len);

    // --- Telemetry state (read by GUI) ---
    Telemetry telemetry() const;
    double speed_kmh() const;
    double speed_mph() const;

    // --- Active board ---
    std::optional<Board> active_board() const;
    bool is_known_board() const;
    const char* guessed_board_type() const;

    // --- CAN devices ---
    std::vector<uint8_t> can_device_ids() const;
    std::vector<CANDevice> can_devices() const;

    // --- FW info ---
    std::optional<FWVersion> main_fw() const;

    // --- Refloat ---
    bool has_refloat() const;
    std::optional<RefloatInfo> refloat_info() const;
    bool refloat_installing() const;
    bool refloat_installed() const;
    void install_refloat();

    // --- Wizard ---
    bool should_show_wizard() const;
    void dismiss_wizard();

    // --- Board CRUD (delegates to storage) ---
    std::vector<Board> boards() const;
    void save_board(const Board& board);
    void remove_board(std::string_view id);

    // --- Profile CRUD (delegates to storage) ---
    std::vector<RiderProfile> profiles() const;
    void save_profile(const RiderProfile& profile);
    void remove_profile(std::string_view id);
    std::string active_profile_id() const;
    void set_active_profile_id(const std::string& id);

    // --- Storage ---
    Storage& storage() { return storage_; }

private:
    mutable std::mutex mu_;
    Storage storage_;
    SendCallback send_cb_;
    StateCallback state_cb_;
    ErrorCallback error_cb_;

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

    // Deferred callback queues (populated under lock, flushed outside)
    std::vector<std::vector<uint8_t>> pending_sends_;
    std::vector<std::string> pending_errors_;
    bool pending_notify_ = false;

    // --- Internal handlers (called with mu_ held) ---
    void handle_values(const uint8_t* data, size_t len);
    void handle_fw_version(const uint8_t* data, size_t len);
    void handle_ping_can(const uint8_t* data, size_t len);
    void handle_custom_app_data(const uint8_t* data, size_t len);
    void handle_write_new_app_data();
    void handle_fw_info(const FWVersion& fw);
    void query_next_can_device();
    bool is_known_board_locked() const; // mu_ already held

    // Deferred callback helpers — queue work while locked, flush after unlock
    void queue_send(const std::vector<uint8_t>& payload);
    void queue_notify();
    void flush_pending(std::unique_lock<std::mutex>& lock);
};

} // namespace nosedive
