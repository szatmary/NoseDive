#include "nosedive/ffi.h"
#include "nosedive/crc.hpp"
#include "nosedive/protocol.hpp"
#include "nosedive/commands.hpp"
#include "nosedive/profile.hpp"
#include "nosedive/refloat.hpp"
#include "nosedive/ble_transport.hpp"
#include <cstdlib>
#include <cstring>

// Opaque wrapper
struct nd_profile {
    nosedive::Profile profile;
};

extern "C" {

uint16_t nd_crc16(const uint8_t* data, size_t len) {
    return nosedive::crc16(data, len);
}

uint8_t* nd_encode_packet(const uint8_t* payload, size_t payload_len, size_t* out_len) {
    auto pkt = nosedive::encode_packet(payload, payload_len);
    if (pkt.empty()) {
        *out_len = 0;
        return nullptr;
    }
    *out_len = pkt.size();
    auto* buf = static_cast<uint8_t*>(std::malloc(pkt.size()));
    std::memcpy(buf, pkt.data(), pkt.size());
    return buf;
}

uint8_t* nd_decode_packet(const uint8_t* data, size_t data_len,
                          size_t* out_len, size_t* consumed) {
    auto result = nosedive::decode_packet(data, data_len);
    if (!result) {
        *out_len = 0;
        *consumed = 0;
        return nullptr;
    }
    *out_len = result->payload.size();
    *consumed = result->bytes_consumed;
    auto* buf = static_cast<uint8_t*>(std::malloc(result->payload.size()));
    std::memcpy(buf, result->payload.data(), result->payload.size());
    return buf;
}

void nd_free(void* ptr) {
    std::free(ptr);
}

bool nd_parse_values(const uint8_t* data, size_t len, nd_values_t* out) {
    auto vals = nosedive::parse_values(data, len);
    if (!vals) return false;
    out->temp_mosfet       = vals->temp_mosfet;
    out->temp_motor        = vals->temp_motor;
    out->avg_motor_current = vals->avg_motor_current;
    out->avg_input_current = vals->avg_input_current;
    out->duty_cycle        = vals->duty_cycle;
    out->rpm               = vals->rpm;
    out->voltage           = vals->voltage;
    out->amp_hours         = vals->amp_hours;
    out->amp_hours_charged = vals->amp_hours_charged;
    out->watt_hours        = vals->watt_hours;
    out->watt_hours_charged = vals->watt_hours_charged;
    out->tachometer        = vals->tachometer;
    out->tachometer_abs    = vals->tachometer_abs;
    out->fault             = static_cast<uint8_t>(vals->fault);
    return true;
}

nd_profile_t* nd_profile_load(const char* json, size_t json_len) {
    auto p = nosedive::load_profile(std::string(json, json_len));
    if (!p) return nullptr;
    auto* handle = new nd_profile{std::move(*p)};
    return handle;
}

nd_profile_t* nd_profile_load_file(const char* path) {
    auto p = nosedive::load_profile_file(path);
    if (!p) return nullptr;
    auto* handle = new nd_profile{std::move(*p)};
    return handle;
}

void nd_profile_free(nd_profile_t* p) {
    delete p;
}

const char* nd_profile_name(const nd_profile_t* p) { return p->profile.name.c_str(); }
const char* nd_profile_manufacturer(const nd_profile_t* p) { return p->profile.manufacturer.c_str(); }
const char* nd_profile_model(const nd_profile_t* p) { return p->profile.model.c_str(); }

int nd_profile_motor_pole_pairs(const nd_profile_t* p) { return p->profile.motor.pole_pairs; }
double nd_profile_motor_resistance(const nd_profile_t* p) { return p->profile.motor.resistance; }
double nd_profile_motor_inductance(const nd_profile_t* p) { return p->profile.motor.inductance; }
double nd_profile_motor_flux_linkage(const nd_profile_t* p) { return p->profile.motor.flux_linkage; }

double nd_profile_battery_voltage_min(const nd_profile_t* p) { return p->profile.battery.voltage_min; }
double nd_profile_battery_voltage_max(const nd_profile_t* p) { return p->profile.battery.voltage_max; }
double nd_profile_battery_voltage_nominal(const nd_profile_t* p) { return p->profile.battery.voltage_nominal; }
double nd_profile_battery_capacity_wh(const nd_profile_t* p) { return p->profile.battery.capacity_wh; }
int nd_profile_battery_series_cells(const nd_profile_t* p) { return p->profile.battery.series_cells; }
int nd_profile_battery_parallel_cells(const nd_profile_t* p) { return p->profile.battery.parallel_cells; }

double nd_profile_erpm_per_mps(const nd_profile_t* p) { return p->profile.erpm_per_mps(); }
double nd_profile_speed_from_erpm(const nd_profile_t* p, double erpm) { return p->profile.speed_from_erpm(erpm); }
double nd_profile_battery_percentage(const nd_profile_t* p, double voltage) { return p->profile.battery_percentage(voltage); }

// --- Packet decoder ---

struct nd_decoder {
    nosedive::PacketDecoder decoder;
};

nd_decoder_t* nd_decoder_create(void) {
    return new nd_decoder{};
}

void nd_decoder_destroy(nd_decoder_t* d) {
    delete d;
}

int nd_decoder_feed(nd_decoder_t* d, const uint8_t* data, size_t len) {
    d->decoder.feed(data, len);
    return static_cast<int>(d->decoder.packet_count());
}

uint8_t* nd_decoder_pop(nd_decoder_t* d, size_t* out_len) {
    auto pkt = d->decoder.pop();
    if (pkt.empty()) {
        *out_len = 0;
        return nullptr;
    }
    *out_len = pkt.size();
    auto* buf = static_cast<uint8_t*>(std::malloc(pkt.size()));
    std::memcpy(buf, pkt.data(), pkt.size());
    return buf;
}

size_t nd_decoder_count(const nd_decoder_t* d) {
    return d->decoder.packet_count();
}

void nd_decoder_reset(nd_decoder_t* d) {
    d->decoder.reset();
}

// --- Refloat ---

static void rtdata_to_c(const nosedive::refloat::RTData& rt, nd_refloat_rtdata_t* out) {
    out->run_state      = static_cast<uint8_t>(rt.state.run_state);
    out->mode           = static_cast<uint8_t>(rt.state.mode);
    out->sat            = static_cast<uint8_t>(rt.state.sat);
    out->stop           = static_cast<uint8_t>(rt.state.stop);
    out->footpad        = static_cast<uint8_t>(rt.state.footpad);
    out->speed          = rt.speed;
    out->erpm           = rt.erpm;
    out->motor_current  = rt.motor_current;
    out->dir_current    = rt.dir_current;
    out->filt_current   = rt.filt_current;
    out->duty_cycle     = rt.duty_cycle;
    out->batt_voltage   = rt.batt_voltage;
    out->batt_current   = rt.batt_current;
    out->mosfet_temp    = rt.mosfet_temp;
    out->motor_temp     = rt.motor_temp;
    out->pitch          = rt.pitch;
    out->balance_pitch  = rt.balance_pitch;
    out->roll           = rt.roll;
    out->adc1           = rt.adc1;
    out->adc2           = rt.adc2;
    out->remote_input   = rt.remote_input;
    out->setpoint       = rt.setpoint;
    out->atr_setpoint   = rt.atr_setpoint;
    out->brake_tilt_setpoint  = rt.brake_tilt_setpoint;
    out->torque_tilt_setpoint = rt.torque_tilt_setpoint;
    out->turn_tilt_setpoint   = rt.turn_tilt_setpoint;
    out->remote_setpoint      = rt.remote_setpoint;
    out->balance_current      = rt.balance_current;
    out->atr_accel_diff       = rt.atr_accel_diff;
    out->atr_speed_boost      = rt.atr_speed_boost;
    out->booster_current      = rt.booster_current;
}

bool nd_refloat_parse_all_data(const uint8_t* data, size_t len, uint8_t mode,
                                nd_refloat_rtdata_t* out) {
    auto rt = nosedive::refloat::parse_all_data(data, len, mode);
    if (!rt) return false;
    rtdata_to_c(*rt, out);
    return true;
}

bool nd_refloat_parse_rt_data(const uint8_t* data, size_t len,
                               nd_refloat_rtdata_t* out) {
    auto rt = nosedive::refloat::parse_rt_data(data, len);
    if (!rt) return false;
    rtdata_to_c(*rt, out);
    return true;
}

bool nd_refloat_parse_info(const uint8_t* data, size_t len, nd_refloat_info_t* out) {
    auto info = nosedive::refloat::parse_info(data, len);
    if (!info) return false;
    std::memset(out, 0, sizeof(*out));
    std::strncpy(out->name, info->name.c_str(), sizeof(out->name) - 1);
    out->major = info->major;
    out->minor = info->minor;
    out->patch = info->patch;
    std::strncpy(out->suffix, info->suffix.c_str(), sizeof(out->suffix) - 1);
    return true;
}

static uint8_t* vec_to_malloc(const std::vector<uint8_t>& v, size_t* out_len) {
    *out_len = v.size();
    auto* buf = static_cast<uint8_t*>(std::malloc(v.size()));
    std::memcpy(buf, v.data(), v.size());
    return buf;
}

uint8_t* nd_refloat_build_get_all_data(uint8_t mode, size_t* out_len) {
    auto cmd = nosedive::refloat::build_get_all_data(mode);
    return vec_to_malloc(cmd, out_len);
}

uint8_t* nd_refloat_build_get_rt_data(size_t* out_len) {
    auto cmd = nosedive::refloat::build_get_rt_data();
    return vec_to_malloc(cmd, out_len);
}

uint8_t* nd_refloat_build_info_request(size_t* out_len) {
    auto cmd = nosedive::refloat::build_info_request();
    return vec_to_malloc(cmd, out_len);
}

// --- BLE Transport ---

struct nd_transport {
    nosedive::BLETransport transport;
    nd_send_callback_t send_cb = nullptr;
    nd_packet_callback_t packet_cb = nullptr;
    void* send_ctx = nullptr;
    void* packet_ctx = nullptr;

    nd_transport(size_t mtu) : transport(mtu) {}
};

nd_transport_t* nd_transport_create(size_t mtu) {
    auto* t = new nd_transport(mtu);
    return t;
}

void nd_transport_destroy(nd_transport_t* t) {
    delete t;
}

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

void nd_transport_set_mtu(nd_transport_t* t, size_t mtu) {
    t->transport.set_mtu(mtu);
}

void nd_transport_receive(nd_transport_t* t, const uint8_t* data, size_t len) {
    t->transport.on_ble_receive(data, len);
}

bool nd_transport_send_payload(nd_transport_t* t, const uint8_t* payload, size_t len) {
    return t->transport.send_payload(payload, len);
}

bool nd_transport_send_command(nd_transport_t* t, uint8_t cmd) {
    return t->transport.send_command(cmd);
}

bool nd_transport_send_custom_app_data(nd_transport_t* t, const uint8_t* data, size_t len) {
    return t->transport.send_custom_app_data(data, len);
}

void nd_transport_reset(nd_transport_t* t) {
    t->transport.reset();
}

} // extern "C"
