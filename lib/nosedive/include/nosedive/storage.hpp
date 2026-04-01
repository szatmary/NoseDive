#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nosedive {

/// A registered board in the user's fleet.
struct Board {
    std::string id;             // UUID from COMM_FW_VERSION (hex string)
    std::string name;
    std::string ble_name;
    std::string ble_address;
    int64_t last_connected = 0; // unix timestamp (seconds)
    bool wizard_complete = false;

    // Hardware info
    std::string hw_name;
    uint8_t fw_major = 0;
    uint8_t fw_minor = 0;
    std::string refloat_version;

    // Board config
    int motor_pole_pairs = 15;
    double wheel_circumference_m = 0.8778;
    int battery_series_cells = 20;
    double battery_voltage_min = 60.0;
    double battery_voltage_max = 84.0;

    // Stats
    double lifetime_distance_m = 0;
    int ride_count = 0;

    // Active rider profile (UUID string, empty if none)
    std::string active_profile_id;
};

/// A rider's tune profile — maps to the radar chart axes.
struct RiderProfile {
    std::string id;         // UUID string
    std::string name;
    std::string icon;       // SF Symbol / material icon name
    bool is_built_in = false;
    int64_t created_at = 0; // unix timestamp
    int64_t modified_at = 0;

    // Radar chart axes (1.0 - 10.0)
    double responsiveness = 5.0;
    double stability = 5.0;
    double carving = 5.0;
    double braking = 5.0;
    double safety = 5.0;
    double agility = 5.0;

    // Startup/disengage
    double footpad_sensitivity = 5.0;
    double disengage_speed = 5.0;
};

/// All persisted app data.
struct AppData {
    std::vector<Board> boards;
    std::vector<RiderProfile> rider_profiles;
    std::string active_profile_id;
};

/// Serialize AppData to JSON string.
std::string app_data_to_json(const AppData& data);

/// Deserialize AppData from JSON string. Returns empty AppData on error.
AppData app_data_from_json(const std::string& json);

/// Save AppData to a file path. Returns true on success.
bool app_data_save(const AppData& data, const std::string& path);

/// Load AppData from a file path. Returns empty AppData if file doesn't exist or is invalid.
AppData app_data_load(const std::string& path);

/// Storage singleton — owns all persisted data, auto-saves on mutation.
class Storage {
public:
    /// Initialize with file path. Loads existing data if present.
    explicit Storage(std::string path);

    // Boards
    const std::vector<Board>& boards() const { return data_.boards; }
    std::optional<std::reference_wrapper<const Board>> find_board(std::string_view id) const;
    void upsert_board(Board board);
    void remove_board(std::string_view id);

    // Profiles
    const std::vector<RiderProfile>& profiles() const { return data_.rider_profiles; }
    std::optional<std::reference_wrapper<const RiderProfile>> find_profile(std::string_view id) const;
    void upsert_profile(RiderProfile profile);
    void remove_profile(std::string_view id);

    // Active profile
    const std::string& active_profile_id() const { return data_.active_profile_id; }
    void set_active_profile_id(std::string id);

    /// Force save (normally automatic after each mutation).
    void save();

private:
    std::string path_;
    AppData data_;

    void persist();
};

} // namespace nosedive
