#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nosedive {

struct Firmware {
    uint8_t major = 6;
    uint8_t minor = 5;
};

struct Controller {
    std::string type;
    std::string hardware;
    Firmware firmware;
    double max_current = 0;
    double max_brake_current = 0;
};

struct Motor {
    std::string type;
    std::string name;
    std::string notes;
    int pole_pairs = 15;
    double resistance = 0;     // ohms
    double inductance = 0;     // henries
    double flux_linkage = 0;   // weber
    double max_current = 0;
    double max_brake_current = 0;
    double kv = 0;
    std::vector<int> hall_sensor_table; // 8 entries
};

struct Battery {
    std::string chemistry;
    std::string cell_type;
    std::string configuration;
    int series_cells = 0;
    int parallel_cells = 0;
    double capacity_ah = 0;
    double capacity_wh = 0;
    double voltage_min = 0;
    double voltage_nominal = 0;
    double voltage_max = 0;
    double cutoff_start = 0;
    double cutoff_end = 0;
    double max_discharge_current = 0;
    double max_charge_current = 0;
    double cell_min_voltage = 0;
    double cell_max_voltage = 0;
    double cell_nominal_voltage = 0;
};

struct Wheel {
    double diameter = 0;
    std::string diameter_unit;
    double tire_pressure_psi = 0;
    double circumference_m = 0;
};

struct Performance {
    double top_speed_mph = 0;
    double range_miles = 0;
    double weight_lbs = 0;
};

struct Profile {
    std::string name;
    std::string manufacturer;
    std::string model;
    std::string description;

    Controller controller;
    Motor motor;
    Battery battery;
    Wheel wheel;
    Performance performance;

    // Computed helpers
    double erpm_per_mps() const;
    double speed_from_erpm(double erpm) const;
    double erpm_from_speed(double mps) const;
    double battery_percentage(double voltage) const;
};

// Load a profile from a JSON string. Returns nullopt on parse error.
// Uses a minimal JSON parser — no external dependencies.
std::optional<Profile> load_profile(const std::string& json);

// Load a profile from a file path.
std::optional<Profile> load_profile_file(const std::string& path);

} // namespace nosedive
