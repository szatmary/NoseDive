#include "nosedive/ffi.h"
#include "nosedive/crc.h"
#include "nosedive/protocol.h"
#include "nosedive/commands.h"
#include "nosedive/profile.h"
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

} // extern "C"
