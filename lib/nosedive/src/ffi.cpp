#include "nosedive/ffi.h"
#include "vesc/commands.hpp"
#include "nosedive/profile.hpp"
#include "vesc/refloat.hpp"
#include "nosedive/engine.hpp"
#include <cstring>
#include <algorithm>
#include <memory>

// --- Helpers ---

static void copy_str(char* dst, size_t dst_size, const std::string& src) {
    size_t n = std::min(src.size(), dst_size - 1);
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

static void board_to_c(const nosedive::Board& b, nd_board_t* out) {
    std::memset(out, 0, sizeof(*out));
    copy_str(out->id, sizeof(out->id), b.id);
    copy_str(out->name, sizeof(out->name), b.name);
    copy_str(out->ble_name, sizeof(out->ble_name), b.ble_name);
    copy_str(out->ble_address, sizeof(out->ble_address), b.ble_address);
    out->last_connected = b.last_connected;
    out->wizard_complete = b.wizard_complete;
    copy_str(out->hw_name, sizeof(out->hw_name), b.hw_name);
    out->fw_major = b.fw_major;
    out->fw_minor = b.fw_minor;
    copy_str(out->refloat_version, sizeof(out->refloat_version), b.refloat_version);
    out->motor_pole_pairs = b.motor_pole_pairs;
    out->wheel_circumference_m = b.wheel_circumference_m;
    out->battery_series_cells = b.battery_series_cells;
    out->battery_voltage_min = b.battery_voltage_min;
    out->battery_voltage_max = b.battery_voltage_max;
    out->lifetime_distance_m = b.lifetime_distance_m;
    out->ride_count = b.ride_count;
    copy_str(out->active_profile_id, sizeof(out->active_profile_id), b.active_profile_id);
}

static nosedive::Board board_from_c(const nd_board_t* b) {
    nosedive::Board out;
    out.id = b->id;
    out.name = b->name;
    out.ble_name = b->ble_name;
    out.ble_address = b->ble_address;
    out.last_connected = b->last_connected;
    out.wizard_complete = b->wizard_complete;
    out.hw_name = b->hw_name;
    out.fw_major = b->fw_major;
    out.fw_minor = b->fw_minor;
    out.refloat_version = b->refloat_version;
    out.motor_pole_pairs = b->motor_pole_pairs;
    out.wheel_circumference_m = b->wheel_circumference_m;
    out.battery_series_cells = b->battery_series_cells;
    out.battery_voltage_min = b->battery_voltage_min;
    out.battery_voltage_max = b->battery_voltage_max;
    out.lifetime_distance_m = b->lifetime_distance_m;
    out.ride_count = b->ride_count;
    out.active_profile_id = b->active_profile_id;
    return out;
}

static void profile_to_c(const nosedive::RiderProfile& p, nd_rider_profile_t* out) {
    std::memset(out, 0, sizeof(*out));
    copy_str(out->id, sizeof(out->id), p.id);
    copy_str(out->name, sizeof(out->name), p.name);
    copy_str(out->icon, sizeof(out->icon), p.icon);
    out->is_built_in = p.is_built_in;
    out->created_at = p.created_at;
    out->modified_at = p.modified_at;
    out->responsiveness = p.responsiveness;
    out->stability = p.stability;
    out->carving = p.carving;
    out->braking = p.braking;
    out->safety = p.safety;
    out->agility = p.agility;
    out->footpad_sensitivity = p.footpad_sensitivity;
    out->disengage_speed = p.disengage_speed;
}

static nosedive::RiderProfile profile_from_c(const nd_rider_profile_t* p) {
    nosedive::RiderProfile out;
    out.id = p->id;
    out.name = p->name;
    out.icon = p->icon;
    out.is_built_in = p->is_built_in;
    out.created_at = p->created_at;
    out.modified_at = p->modified_at;
    out.responsiveness = p->responsiveness;
    out.stability = p->stability;
    out.carving = p->carving;
    out.braking = p->braking;
    out.safety = p->safety;
    out.agility = p->agility;
    out.footpad_sensitivity = p->footpad_sensitivity;
    out.disengage_speed = p->disengage_speed;
    return out;
}

// --- Engine opaque wrapper ---

struct nd_engine {
    nosedive::Engine engine;

    nd_write_cb write_cb = nullptr;
    void* write_ctx = nullptr;

    nd_telemetry_cb telemetry_cb = nullptr;
    void* telemetry_ctx = nullptr;

    nd_board_cb board_cb = nullptr;
    void* board_ctx = nullptr;

    nd_refloat_cb refloat_cb = nullptr;
    void* refloat_ctx = nullptr;

    nd_can_cb can_cb = nullptr;
    void* can_ctx = nullptr;

    nd_error_cb error_cb = nullptr;
    void* error_ctx = nullptr;

    explicit nd_engine(const char* path) : engine(path) {}
};

extern "C" {

// --- Engine lifecycle ---

nd_engine_t* nd_engine_create(const char* storage_path) {
    return std::make_unique<nd_engine>(storage_path).release();
}

void nd_engine_destroy(nd_engine_t* e) {
    std::unique_ptr<nd_engine> p(e);
}

// --- Platform → Engine ---

void nd_engine_receive_bytes(nd_engine_t* e, const uint8_t* data, size_t len) {
    e->engine.receive_bytes(data, len);
}

void nd_engine_on_connected(nd_engine_t* e, size_t mtu) {
    e->engine.on_connected(mtu);
}

void nd_engine_on_disconnected(nd_engine_t* e) {
    e->engine.on_disconnected();
}

// --- Actions ---

void nd_engine_install_refloat(nd_engine_t* e) {
    e->engine.install_refloat();
}

void nd_engine_dismiss_wizard(nd_engine_t* e) {
    e->engine.dismiss_wizard();
}

// --- Callbacks ---

void nd_engine_set_write_callback(nd_engine_t* e, nd_write_cb cb, void* ctx) {
    e->write_cb = cb;
    e->write_ctx = ctx;
    e->engine.set_write_callback([e](const uint8_t* data, size_t len) {
        if (e->write_cb) e->write_cb(data, len, e->write_ctx);
    });
}

void nd_engine_set_telemetry_callback(nd_engine_t* e, nd_telemetry_cb cb, void* ctx) {
    e->telemetry_cb = cb;
    e->telemetry_ctx = ctx;
    e->engine.set_telemetry_callback([e](const nosedive::Telemetry& t) {
        if (!e->telemetry_cb) return;
        nd_telemetry_t ct{};
        ct.temp_mosfet     = t.temp_mosfet;
        ct.temp_motor      = t.temp_motor;
        ct.motor_current   = t.motor_current;
        ct.battery_current = t.battery_current;
        ct.duty_cycle      = t.duty_cycle;
        ct.erpm            = t.erpm;
        ct.battery_voltage = t.battery_voltage;
        ct.battery_percent = t.battery_percent;
        ct.speed           = t.speed;
        ct.power           = t.power;
        ct.tachometer      = t.tachometer;
        ct.tachometer_abs  = t.tachometer_abs;
        ct.fault           = static_cast<uint8_t>(t.fault);
        e->telemetry_cb(ct, e->telemetry_ctx);
    });
}

void nd_engine_set_board_callback(nd_engine_t* e, nd_board_cb cb, void* ctx) {
    e->board_cb = cb;
    e->board_ctx = ctx;
    e->engine.set_board_callback([e](const nosedive::Board& board, const nosedive::FWVersionResponse& fw, bool show_wizard, bool is_known) {
        if (!e->board_cb) return;
        nd_board_event_t evt{};
        copy_str(evt.id, sizeof(evt.id), board.id);
        copy_str(evt.name, sizeof(evt.name), board.name);
        copy_str(evt.hw_name, sizeof(evt.hw_name), fw.hw_name);
        evt.fw_major = fw.major;
        evt.fw_minor = fw.minor;
        copy_str(evt.uuid, sizeof(evt.uuid), fw.uuid);
        evt.hw_type = static_cast<uint8_t>(fw.hw_type);
        evt.custom_config_count = fw.custom_config_count;
        copy_str(evt.package_name, sizeof(evt.package_name), fw.package_name);
        evt.show_wizard = show_wizard;
        evt.is_known = is_known;
        e->board_cb(evt, e->board_ctx);
    });
}

void nd_engine_set_refloat_callback(nd_engine_t* e, nd_refloat_cb cb, void* ctx) {
    e->refloat_cb = cb;
    e->refloat_ctx = ctx;
    e->engine.set_refloat_callback([e](bool has_refloat, const std::optional<nosedive::RefloatInfo>& info, bool installing, bool installed) {
        if (!e->refloat_cb) return;
        nd_refloat_event_t evt{};
        evt.has_refloat = has_refloat;
        if (info) {
            copy_str(evt.name, sizeof(evt.name), info->name);
            evt.major = info->major;
            evt.minor = info->minor;
            evt.patch = info->patch;
            copy_str(evt.suffix, sizeof(evt.suffix), info->suffix);
        }
        evt.installing = installing;
        evt.installed = installed;
        e->refloat_cb(evt, e->refloat_ctx);
    });
}

void nd_engine_set_can_callback(nd_engine_t* e, nd_can_cb cb, void* ctx) {
    e->can_cb = cb;
    e->can_ctx = ctx;
    e->engine.set_can_callback([e](const std::vector<uint8_t>& ids) {
        if (!e->can_cb) return;
        e->can_cb(ids.data(), ids.size(), e->can_ctx);
    });
}

void nd_engine_set_error_callback(nd_engine_t* e, nd_error_cb cb, void* ctx) {
    e->error_cb = cb;
    e->error_ctx = ctx;
    e->engine.set_error_callback([e](const char* msg) {
        if (e->error_cb) e->error_cb(msg, e->error_ctx);
    });
}

// --- Wizard ---

static nd_setup_step_t setup_step_to_c(nosedive::SetupStep step) {
    switch (step) {
        case nosedive::SetupStep::Idle:            return ND_SETUP_IDLE;
        case nosedive::SetupStep::CheckFWExpress: return ND_SETUP_CHECK_FW_EXPRESS;
        case nosedive::SetupStep::CheckFWBMS: return ND_SETUP_CHECK_FW_BMS;
        case nosedive::SetupStep::CheckFWVESC: return ND_SETUP_CHECK_FW_VESC;
        case nosedive::SetupStep::UpdateFW:        return ND_SETUP_UPDATE_FW;
        case nosedive::SetupStep::WaitReconnect:  return ND_SETUP_WAIT_RECONNECT;
        case nosedive::SetupStep::InstallRefloat:  return ND_SETUP_INSTALL_REFLOAT;
        case nosedive::SetupStep::DetectBattery:   return ND_SETUP_DETECT_BATTERY;
        case nosedive::SetupStep::DetectFootpads:  return ND_SETUP_DETECT_FOOTPADS;
        case nosedive::SetupStep::CalibrateIMU:    return ND_SETUP_CALIBRATE_IMU;
        case nosedive::SetupStep::DetectMotor:     return ND_SETUP_DETECT_MOTOR;
        case nosedive::SetupStep::ConfigureWheel:  return ND_SETUP_CONFIGURE_WHEEL;
        case nosedive::SetupStep::Done:            return ND_SETUP_DONE;
        default:                                    return ND_SETUP_IDLE;
    }
}

void nd_engine_set_setup_callback(nd_engine_t* e, nd_setup_cb cb, void* ctx) {
    e->engine.set_setup_callback([e, cb, ctx](const nosedive::SetupState& ws) {
        if (!cb) return;
        nd_setup_state_t cws = {};
        cws.step = setup_step_to_c(ws.step);
        copy_str(cws.error, sizeof(cws.error), ws.error);
        copy_str(cws.detail, sizeof(cws.detail), ws.detail);
        cb(cws, ctx);
    });
}

void nd_engine_setup_start(nd_engine_t* e)  { e->engine.setup_start(); }
void nd_engine_setup_retry(nd_engine_t* e)  { e->engine.setup_retry(); }
void nd_engine_setup_skip(nd_engine_t* e)   { e->engine.setup_skip(); }
void nd_engine_setup_update(nd_engine_t* e) { e->engine.setup_update(); }
void nd_engine_setup_abort(nd_engine_t* e)  { e->engine.setup_abort(); }

// --- Board fleet ---

size_t nd_engine_board_count(const nd_engine_t* e) {
    return e->engine.boards().size();
}

nd_board_t nd_engine_get_board(const nd_engine_t* e, size_t index) {
    nd_board_t out{};
    auto boards = e->engine.boards();
    if (index < boards.size()) board_to_c(boards[index], &out);
    return out;
}

void nd_engine_save_board(nd_engine_t* e, nd_board_t board) {
    e->engine.save_board(board_from_c(&board));
}

void nd_engine_remove_board(nd_engine_t* e, const char* id) {
    e->engine.remove_board(id);
}

// --- Rider profiles ---

size_t nd_engine_profile_count(const nd_engine_t* e) {
    return e->engine.profiles().size();
}

nd_rider_profile_t nd_engine_get_profile(const nd_engine_t* e, size_t index) {
    nd_rider_profile_t out{};
    const auto& profiles = e->engine.profiles();
    if (index < profiles.size()) profile_to_c(profiles[index], &out);
    return out;
}

void nd_engine_save_profile(nd_engine_t* e, nd_rider_profile_t profile) {
    e->engine.save_profile(profile_from_c(&profile));
}

void nd_engine_remove_profile(nd_engine_t* e, const char* id) {
    e->engine.remove_profile(id);
}

const char* nd_engine_active_profile_id(const nd_engine_t* e) {
    static thread_local std::string result;
    result = e->engine.active_profile_id();
    return result.c_str();
}

void nd_engine_set_active_profile_id(nd_engine_t* e, const char* profile_id) {
    e->engine.set_active_profile_id(profile_id ? profile_id : "");
}

} // extern "C"
