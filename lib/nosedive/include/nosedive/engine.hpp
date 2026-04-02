#pragma once

#include "vesc/commands.hpp"
#include "vesc/protocol.hpp"
#include "vesc/vescpkg.hpp"
#include "nosedive/storage.hpp"
#include "nosedive/setupboard.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nosedive {

using vesc::Telemetry;
using FWVersionResponse = vesc::FWVersion::Response;
using vesc::RefloatInfo;
using vesc::PacketDecoder;
using vesc::CommPacketID;
using vesc::encode_packet;


/// Callback to write raw bytes to the wire (BLE/TCP). Platform implements this.
using WriteCallback = std::function<void(const uint8_t* data, size_t len)>;

/// Domain callbacks — engine pushes parsed state to the platform.
using TelemetryCallback = std::function<void(const Telemetry&)>;
using BoardCallback = std::function<void(const Board&, const FWVersionResponse&, bool show_wizard, bool is_known)>;
using RefloatCallback = std::function<void(bool has_refloat, const std::optional<RefloatInfo>&, bool installing, bool installed)>;
using CANCallback = std::function<void(const std::vector<uint8_t>& ids)>;
using ErrorCallback = std::function<void(const char* message)>;

/// CAN device info discovered during enumeration.
struct CANDevice {
    uint8_t controller_id = 0;
    FWVersionResponse fw;
};

// SetupStep, SetupState, SetupCallback are in setupboard.hpp

/// The application engine. Owns all business logic, protocol codec, and state.
/// Platform layer (Swift/Kotlin) feeds raw bytes in, receives parsed structs via callbacks.
class Engine {
public:
    explicit Engine(const std::string& storage_path);

    // --- Callbacks (set before connecting) ---
    void set_write_callback(WriteCallback cb);
    void set_telemetry_callback(TelemetryCallback cb);
    void set_board_callback(BoardCallback cb);
    void set_refloat_callback(RefloatCallback cb);
    void set_can_callback(CANCallback cb);
    void set_error_callback(ErrorCallback cb);

    // --- Platform → Engine ---
    void receive_bytes(const uint8_t* data, size_t len);
    void on_connected(size_t mtu = 512);
    void on_disconnected();

    // --- Actions ---
    void install_refloat();
    void dismiss_wizard();

    /// Load a Refloat .vescpkg for the setup wizard to install.
    /// Returns true if the package was parsed successfully.
    bool load_refloat_package(const uint8_t* data, size_t len);

    // --- Wizard ---
    void set_setup_callback(SetupCallback cb);
    void setup_start();
    void setup_retry();
    void setup_skip();
    void setup_update();
    void setup_abort();

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
    PacketDecoder decoder_;
    size_t mtu_ = 512;

    // Callbacks
    WriteCallback write_cb_;
    TelemetryCallback telemetry_cb_;
    BoardCallback board_cb_;
    RefloatCallback refloat_cb_;
    CANCallback can_cb_;
    ErrorCallback error_cb_;

    // Live state
    Telemetry telemetry_;
    std::optional<Board> active_board_;
    std::optional<FWVersionResponse> main_fw_;
    std::optional<RefloatInfo> refloat_info_;

    // CAN discovery
    std::vector<uint8_t> can_device_ids_;
    std::vector<CANDevice> can_devices_;
    std::vector<uint8_t> pending_can_queries_;
    std::optional<uint8_t> awaiting_fw_from_;

    // Refloat install
    bool refloat_installing_ = false;
    bool refloat_installed_ = false;

    // Setup wizard
    bool show_wizard_ = false;
    SetupBoard setup_;
    SetupCallback setup_external_cb_;
    bool pending_setup_ = false;
    std::optional<vesc::VescPackage> refloat_pkg_;

    // Board type guess result (cached)
    mutable std::string guessed_type_cache_;

    // Deferred work queues (populated under lock, flushed outside)
    std::vector<std::vector<uint8_t>> pending_sends_;
    std::vector<std::string> pending_errors_;
    bool pending_telemetry_ = false;
    bool pending_board_ = false;
    bool pending_refloat_ = false;
    bool pending_can_ = false;

    // --- Internal handlers (called with mu_ held) ---
    void handle_payload(const uint8_t* data, size_t len);
    void handle_values(const uint8_t* data, size_t len);
    void handle_fw_version(const uint8_t* data, size_t len);
    void handle_ping_can(const uint8_t* data, size_t len);
    void handle_custom_app_data(const uint8_t* data, size_t len);
    void handle_write_new_app_data();
    void handle_fw_info(const FWVersionResponse& fw);
    void query_next_can_device();
    bool is_known_board_locked() const;

    // Deferred work helpers
    void queue_send(const std::vector<uint8_t>& payload);
    void flush_pending(std::unique_lock<std::mutex>& lock);
};

} // namespace nosedive
