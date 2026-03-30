#pragma once

// C API for FFI from Swift (direct) and Kotlin (JNI wrapper).
// All functions use C linkage and plain types — no C++ types cross the boundary.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- CRC ---

uint16_t nd_crc16(const uint8_t* data, size_t len);

// --- Packet framing ---

// Encode payload into VESC packet. Caller must free result with nd_free().
// Returns NULL on error. Sets *out_len to the packet length.
uint8_t* nd_encode_packet(const uint8_t* payload, size_t payload_len, size_t* out_len);

// Decode one VESC packet from buffer. Caller must free result with nd_free().
// Returns NULL if no complete packet found. Sets *out_len and *consumed.
uint8_t* nd_decode_packet(const uint8_t* data, size_t data_len,
                          size_t* out_len, size_t* consumed);

// Free memory allocated by nd_ functions.
void nd_free(void* ptr);

// --- Values parsing ---

typedef struct {
    double temp_mosfet;
    double temp_motor;
    double avg_motor_current;
    double avg_input_current;
    double duty_cycle;
    double rpm;
    double voltage;
    double amp_hours;
    double amp_hours_charged;
    double watt_hours;
    double watt_hours_charged;
    int32_t tachometer;
    int32_t tachometer_abs;
    uint8_t fault;
} nd_values_t;

// Parse COMM_GET_VALUES response (after command byte). Returns false on error.
bool nd_parse_values(const uint8_t* data, size_t len, nd_values_t* out);

// --- Board profile ---

// Opaque profile handle
typedef struct nd_profile nd_profile_t;

// Load profile from JSON string. Returns NULL on error.
nd_profile_t* nd_profile_load(const char* json, size_t json_len);

// Load profile from file path. Returns NULL on error.
nd_profile_t* nd_profile_load_file(const char* path);

// Free a profile.
void nd_profile_free(nd_profile_t* p);

// Profile accessors — returned strings are valid until the profile is freed.
const char* nd_profile_name(const nd_profile_t* p);
const char* nd_profile_manufacturer(const nd_profile_t* p);
const char* nd_profile_model(const nd_profile_t* p);

// Motor info
int nd_profile_motor_pole_pairs(const nd_profile_t* p);
double nd_profile_motor_resistance(const nd_profile_t* p);
double nd_profile_motor_inductance(const nd_profile_t* p);
double nd_profile_motor_flux_linkage(const nd_profile_t* p);

// Battery info
double nd_profile_battery_voltage_min(const nd_profile_t* p);
double nd_profile_battery_voltage_max(const nd_profile_t* p);
double nd_profile_battery_voltage_nominal(const nd_profile_t* p);
double nd_profile_battery_capacity_wh(const nd_profile_t* p);
int nd_profile_battery_series_cells(const nd_profile_t* p);
int nd_profile_battery_parallel_cells(const nd_profile_t* p);

// Computed values
double nd_profile_erpm_per_mps(const nd_profile_t* p);
double nd_profile_speed_from_erpm(const nd_profile_t* p, double erpm);
double nd_profile_battery_percentage(const nd_profile_t* p, double voltage);

#ifdef __cplusplus
}
#endif
