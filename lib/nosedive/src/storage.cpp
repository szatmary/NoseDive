#include "nosedive/storage.hpp"
#include "../third_party/nlohmann_json.hpp"
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace nosedive {

// --- Board JSON ---

static json board_to_json(const Board& b) {
    return json{
        {"id", b.id},
        {"name", b.name},
        {"ble_name", b.ble_name},
        {"ble_address", b.ble_address},
        {"last_connected", b.last_connected},
        {"wizard_complete", b.wizard_complete},
        {"hw_name", b.hw_name},
        {"fw_major", b.fw_major},
        {"fw_minor", b.fw_minor},
        {"refloat_version", b.refloat_version},
        {"motor_pole_pairs", b.motor_pole_pairs},
        {"wheel_circumference_m", b.wheel_circumference_m},
        {"battery_series_cells", b.battery_series_cells},
        {"battery_voltage_min", b.battery_voltage_min},
        {"battery_voltage_max", b.battery_voltage_max},
        {"lifetime_distance_m", b.lifetime_distance_m},
        {"ride_count", b.ride_count},
        {"active_profile_id", b.active_profile_id},
    };
}

// Safe JSON field extractor — reads a field only if present
template<typename T>
static void read_field(const json& j, const char* key, T& out) {
    if (auto it = j.find(key); it != j.end()) {
        out = it->get<T>();
    }
}

static Board board_from_json(const json& j) {
    Board b;
    read_field(j, "id", b.id);
    read_field(j, "name", b.name);
    read_field(j, "ble_name", b.ble_name);
    read_field(j, "ble_address", b.ble_address);
    read_field(j, "last_connected", b.last_connected);
    read_field(j, "wizard_complete", b.wizard_complete);
    read_field(j, "hw_name", b.hw_name);
    read_field(j, "fw_major", b.fw_major);
    read_field(j, "fw_minor", b.fw_minor);
    read_field(j, "refloat_version", b.refloat_version);
    read_field(j, "motor_pole_pairs", b.motor_pole_pairs);
    read_field(j, "wheel_circumference_m", b.wheel_circumference_m);
    read_field(j, "battery_series_cells", b.battery_series_cells);
    read_field(j, "battery_voltage_min", b.battery_voltage_min);
    read_field(j, "battery_voltage_max", b.battery_voltage_max);
    read_field(j, "lifetime_distance_m", b.lifetime_distance_m);
    read_field(j, "ride_count", b.ride_count);
    read_field(j, "active_profile_id", b.active_profile_id);
    return b;
}

// --- RiderProfile JSON ---

static json profile_to_json(const RiderProfile& p) {
    return json{
        {"id", p.id},
        {"name", p.name},
        {"icon", p.icon},
        {"is_built_in", p.is_built_in},
        {"created_at", p.created_at},
        {"modified_at", p.modified_at},
        {"responsiveness", p.responsiveness},
        {"stability", p.stability},
        {"carving", p.carving},
        {"braking", p.braking},
        {"safety", p.safety},
        {"agility", p.agility},
        {"footpad_sensitivity", p.footpad_sensitivity},
        {"disengage_speed", p.disengage_speed},
    };
}

static RiderProfile profile_from_json(const json& j) {
    RiderProfile p;
    read_field(j, "id", p.id);
    read_field(j, "name", p.name);
    read_field(j, "icon", p.icon);
    read_field(j, "is_built_in", p.is_built_in);
    read_field(j, "created_at", p.created_at);
    read_field(j, "modified_at", p.modified_at);
    read_field(j, "responsiveness", p.responsiveness);
    read_field(j, "stability", p.stability);
    read_field(j, "carving", p.carving);
    read_field(j, "braking", p.braking);
    read_field(j, "safety", p.safety);
    read_field(j, "agility", p.agility);
    read_field(j, "footpad_sensitivity", p.footpad_sensitivity);
    read_field(j, "disengage_speed", p.disengage_speed);
    return p;
}

// --- AppData ---

std::string app_data_to_json(const AppData& data) {
    auto to_array = [](const auto& items, auto&& converter) {
        auto arr = json::array();
        for (const auto& item : items) arr.push_back(converter(item));
        return arr;
    };

    json j;
    j["boards"] = to_array(data.boards, board_to_json);
    j["rider_profiles"] = to_array(data.rider_profiles, profile_to_json);
    j["active_profile_id"] = data.active_profile_id;
    return j.dump(2);
}

AppData app_data_from_json(const std::string& json_str) {
    AppData data;
    try {
        auto j = json::parse(json_str);

        auto from_array = [](const json& j, const char* key, auto&& converter, auto& out) {
            if (auto it = j.find(key); it != j.end() && it->is_array()) {
                out.reserve(it->size());
                for (const auto& item : *it) out.push_back(converter(item));
            }
        };

        from_array(j, "boards", board_from_json, data.boards);
        from_array(j, "rider_profiles", profile_from_json, data.rider_profiles);
        read_field(j, "active_profile_id", data.active_profile_id);
    } catch (...) {
        // Return empty data on parse error
    }
    return data;
}

bool app_data_save(const AppData& data, const std::string& path) {
    std::string json_str = app_data_to_json(data);
    // Write to temp file then rename for atomic save
    std::string tmp_path = path + ".tmp";
    std::ofstream f(tmp_path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(json_str.data(), json_str.size());
    f.close();
    if (!f.good()) return false;
    // Atomic rename
    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        std::remove(tmp_path.c_str());
        return false;
    }
    return true;
}

AppData app_data_load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return app_data_from_json(ss.str());
}

} // namespace nosedive
