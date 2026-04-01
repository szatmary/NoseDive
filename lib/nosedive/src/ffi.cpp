#include "nosedive/ffi.h"
#include "nosedive/crc.hpp"
#include "nosedive/protocol.hpp"
#include "nosedive/commands.hpp"
#include "nosedive/profile.hpp"
#include "nosedive/refloat.hpp"
#include "nosedive/ble_transport.hpp"
#include "nosedive/engine.hpp"
#include <cstdlib>
#include <cstring>
#include <algorithm>

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
    nd_engine_send_cb send_cb = nullptr;
    void* send_ctx = nullptr;
    nd_engine_state_cb state_cb = nullptr;
    void* state_ctx = nullptr;

    explicit nd_engine(const char* path) : engine(path) {}
};

extern "C" {

// --- Engine lifecycle ---

nd_engine_t* nd_engine_create(const char* storage_path) {
    auto* e = new nd_engine(storage_path);
    return e;
}

void nd_engine_destroy(nd_engine_t* e) {
    delete e;
}

void nd_engine_set_send_callback(nd_engine_t* e, nd_engine_send_cb cb, void* ctx) {
    e->send_cb = cb;
    e->send_ctx = ctx;
    e->engine.set_send_callback([e](const uint8_t* data, size_t len) {
        if (e->send_cb) e->send_cb(data, len, e->send_ctx);
    });
}

void nd_engine_set_state_callback(nd_engine_t* e, nd_engine_state_cb cb, void* ctx) {
    e->state_cb = cb;
    e->state_ctx = ctx;
    e->engine.set_state_callback([e]() {
        if (e->state_cb) e->state_cb(e->state_ctx);
    });
}

void nd_engine_on_connected(nd_engine_t* e) {
    e->engine.on_connected();
}

void nd_engine_on_disconnected(nd_engine_t* e) {
    e->engine.on_disconnected();
}

void nd_engine_handle_payload(nd_engine_t* e, const uint8_t* data, size_t len) {
    e->engine.handle_payload(data, len);
}

// --- Telemetry ---

void nd_engine_get_telemetry(const nd_engine_t* e, nd_telemetry_t* out) {
    const auto& t = e->engine.telemetry();
    out->temp_mosfet     = t.temp_mosfet;
    out->temp_motor      = t.temp_motor;
    out->motor_current   = t.motor_current;
    out->battery_current = t.battery_current;
    out->duty_cycle      = t.duty_cycle;
    out->erpm            = t.erpm;
    out->battery_voltage = t.battery_voltage;
    out->battery_percent = t.battery_percent;
    out->speed           = t.speed;
    out->power           = t.power;
    out->tachometer      = t.tachometer;
    out->tachometer_abs  = t.tachometer_abs;
    out->fault           = static_cast<uint8_t>(t.fault);
}

double nd_engine_speed_kmh(const nd_engine_t* e) {
    return e->engine.speed_kmh();
}

double nd_engine_speed_mph(const nd_engine_t* e) {
    return e->engine.speed_mph();
}

// --- Active board ---

bool nd_engine_has_active_board(const nd_engine_t* e) {
    return e->engine.active_board() != nullptr;
}

bool nd_engine_get_active_board(const nd_engine_t* e, nd_board_t* out) {
    auto* b = e->engine.active_board();
    if (!b) return false;
    board_to_c(*b, out);
    return true;
}

bool nd_engine_is_known_board(const nd_engine_t* e) {
    return e->engine.is_known_board();
}

const char* nd_engine_guessed_board_type(const nd_engine_t* e) {
    return e->engine.guessed_board_type();
}

// --- CAN devices ---

size_t nd_engine_can_device_count(const nd_engine_t* e) {
    return e->engine.can_device_ids().size();
}

uint8_t nd_engine_can_device_id(const nd_engine_t* e, size_t index) {
    const auto& ids = e->engine.can_device_ids();
    return index < ids.size() ? ids[index] : 0;
}

// --- Firmware info ---

bool nd_engine_get_main_fw(const nd_engine_t* e, nd_fw_version_t* out) {
    auto* fw = e->engine.main_fw();
    if (!fw) return false;
    std::memset(out, 0, sizeof(*out));
    out->major = fw->major;
    out->minor = fw->minor;
    copy_str(out->hw_name, sizeof(out->hw_name), fw->hw_name);
    copy_str(out->uuid, sizeof(out->uuid), fw->uuid);
    out->hw_type = static_cast<uint8_t>(fw->hw_type);
    out->custom_config_count = fw->custom_config_count;
    copy_str(out->package_name, sizeof(out->package_name), fw->package_name);
    return true;
}

// --- Refloat ---

bool nd_engine_has_refloat(const nd_engine_t* e) {
    return e->engine.has_refloat();
}

bool nd_engine_get_refloat_info(const nd_engine_t* e, nd_refloat_info_t* out) {
    auto* info = e->engine.refloat_info();
    if (!info) return false;
    std::memset(out, 0, sizeof(*out));
    copy_str(out->name, sizeof(out->name), info->name);
    out->major = info->major;
    out->minor = info->minor;
    out->patch = info->patch;
    copy_str(out->suffix, sizeof(out->suffix), info->suffix);
    return true;
}

bool nd_engine_refloat_installing(const nd_engine_t* e) {
    return e->engine.refloat_installing();
}

bool nd_engine_refloat_installed(const nd_engine_t* e) {
    return e->engine.refloat_installed();
}

void nd_engine_install_refloat(nd_engine_t* e) {
    e->engine.install_refloat();
}

// --- Wizard ---

bool nd_engine_should_show_wizard(const nd_engine_t* e) {
    return e->engine.should_show_wizard();
}

void nd_engine_dismiss_wizard(nd_engine_t* e) {
    e->engine.dismiss_wizard();
}

// --- Board fleet ---

size_t nd_engine_board_count(const nd_engine_t* e) {
    return e->engine.boards().size();
}

bool nd_engine_get_board(const nd_engine_t* e, size_t index, nd_board_t* out) {
    const auto& boards = e->engine.boards();
    if (index >= boards.size()) return false;
    board_to_c(boards[index], out);
    return true;
}

void nd_engine_save_board(nd_engine_t* e, const nd_board_t* board) {
    e->engine.save_board(board_from_c(board));
}

void nd_engine_remove_board(nd_engine_t* e, const char* id) {
    e->engine.remove_board(id);
}

// --- Rider profiles ---

size_t nd_engine_profile_count(const nd_engine_t* e) {
    return e->engine.profiles().size();
}

bool nd_engine_get_profile(const nd_engine_t* e, size_t index, nd_rider_profile_t* out) {
    const auto& profiles = e->engine.profiles();
    if (index >= profiles.size()) return false;
    profile_to_c(profiles[index], out);
    return true;
}

void nd_engine_save_profile(nd_engine_t* e, const nd_rider_profile_t* profile) {
    e->engine.save_profile(profile_from_c(profile));
}

void nd_engine_remove_profile(nd_engine_t* e, const char* id) {
    e->engine.remove_profile(id);
}

const char* nd_engine_active_profile_id(const nd_engine_t* e) {
    return e->engine.active_profile_id().c_str();
}

void nd_engine_set_active_profile_id(nd_engine_t* e, const char* profile_id) {
    e->engine.set_active_profile_id(profile_id ? profile_id : "");
}

// --- Low-level packet framing ---

uint16_t nd_crc16(const uint8_t* data, size_t len) {
    return nosedive::crc16(data, len);
}

uint8_t* nd_encode_packet(const uint8_t* payload, size_t payload_len, size_t* out_len) {
    auto pkt = nosedive::encode_packet(payload, payload_len);
    if (pkt.empty()) { *out_len = 0; return nullptr; }
    *out_len = pkt.size();
    auto* buf = static_cast<uint8_t*>(std::malloc(pkt.size()));
    std::memcpy(buf, pkt.data(), pkt.size());
    return buf;
}

uint8_t* nd_decode_packet(const uint8_t* data, size_t data_len,
                          size_t* out_len, size_t* consumed) {
    auto result = nosedive::decode_packet(data, data_len);
    if (!result) { *out_len = 0; *consumed = 0; return nullptr; }
    *out_len = result->payload.size();
    *consumed = result->bytes_consumed;
    auto* buf = static_cast<uint8_t*>(std::malloc(result->payload.size()));
    std::memcpy(buf, result->payload.data(), result->payload.size());
    return buf;
}

void nd_free(void* ptr) {
    std::free(ptr);
}

// --- Packet decoder ---

struct nd_decoder {
    nosedive::PacketDecoder decoder;
};

nd_decoder_t* nd_decoder_create(void) { return new nd_decoder{}; }
void nd_decoder_destroy(nd_decoder_t* d) { delete d; }

int nd_decoder_feed(nd_decoder_t* d, const uint8_t* data, size_t len) {
    d->decoder.feed(data, len);
    return static_cast<int>(d->decoder.packet_count());
}

uint8_t* nd_decoder_pop(nd_decoder_t* d, size_t* out_len) {
    auto pkt = d->decoder.pop();
    if (pkt.empty()) { *out_len = 0; return nullptr; }
    *out_len = pkt.size();
    auto* buf = static_cast<uint8_t*>(std::malloc(pkt.size()));
    std::memcpy(buf, pkt.data(), pkt.size());
    return buf;
}

size_t nd_decoder_count(const nd_decoder_t* d) { return d->decoder.packet_count(); }
void nd_decoder_reset(nd_decoder_t* d) { d->decoder.reset(); }

// --- BLE Transport ---

struct nd_transport {
    nosedive::BLETransport transport;
    nd_send_callback_t send_cb = nullptr;
    nd_packet_callback_t packet_cb = nullptr;
    void* send_ctx = nullptr;
    void* packet_ctx = nullptr;

    nd_transport(size_t mtu) : transport(mtu) {}
};

nd_transport_t* nd_transport_create(size_t mtu) { return new nd_transport(mtu); }
void nd_transport_destroy(nd_transport_t* t) { delete t; }

void nd_transport_set_send_callback(nd_transport_t* t, nd_send_callback_t cb, void* ctx) {
    t->send_cb = cb;
    t->send_ctx = ctx;
    t->transport.set_send_callback([t](const uint8_t* data, size_t len) {
        if (t->send_cb) t->send_cb(data, len, t->send_ctx);
    });
}

void nd_transport_set_packet_callback(nd_transport_t* t, nd_packet_callback_t cb, void* ctx) {
    t->packet_cb = cb;
    t->packet_ctx = ctx;
    t->transport.set_packet_callback([t](const uint8_t* payload, size_t len) {
        if (t->packet_cb) t->packet_cb(payload, len, t->packet_ctx);
    });
}

void nd_transport_set_mtu(nd_transport_t* t, size_t mtu) { t->transport.set_mtu(mtu); }
void nd_transport_receive(nd_transport_t* t, const uint8_t* data, size_t len) { t->transport.on_ble_receive(data, len); }
bool nd_transport_send_payload(nd_transport_t* t, const uint8_t* payload, size_t len) { return t->transport.send_payload(payload, len); }
bool nd_transport_send_command(nd_transport_t* t, uint8_t cmd) { return t->transport.send_command(cmd); }
bool nd_transport_send_custom_app_data(nd_transport_t* t, const uint8_t* data, size_t len) { return t->transport.send_custom_app_data(data, len); }
void nd_transport_reset(nd_transport_t* t) { t->transport.reset(); }

} // extern "C"
