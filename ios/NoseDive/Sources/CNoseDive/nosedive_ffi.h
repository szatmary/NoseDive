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

// --- Packet decoder (push-based, for BLE chunk reassembly) ---

typedef struct nd_decoder nd_decoder_t;

nd_decoder_t* nd_decoder_create(void);
void nd_decoder_destroy(nd_decoder_t* d);

// Feed raw BLE bytes. Returns number of complete packets now available.
int nd_decoder_feed(nd_decoder_t* d, const uint8_t* data, size_t len);

// Pop next complete payload. Caller must free with nd_free().
// Returns NULL if no packet available. Sets *out_len.
uint8_t* nd_decoder_pop(nd_decoder_t* d, size_t* out_len);

// Number of complete packets available.
size_t nd_decoder_count(const nd_decoder_t* d);

void nd_decoder_reset(nd_decoder_t* d);

// --- Refloat ---

typedef struct {
    // State
    uint8_t run_state;   // RunState enum
    uint8_t mode;        // Mode enum
    uint8_t sat;         // SAT enum
    uint8_t stop;        // StopCondition enum
    uint8_t footpad;     // FootpadState enum

    // Motor
    double speed;
    double erpm;
    double motor_current;
    double dir_current;
    double filt_current;
    double duty_cycle;
    double batt_voltage;
    double batt_current;
    double mosfet_temp;
    double motor_temp;

    // IMU
    double pitch;
    double balance_pitch;
    double roll;

    // Footpad ADC
    double adc1;
    double adc2;

    // Remote
    double remote_input;

    // Setpoints
    double setpoint;
    double atr_setpoint;
    double brake_tilt_setpoint;
    double torque_tilt_setpoint;
    double turn_tilt_setpoint;
    double remote_setpoint;
    double balance_current;
    double atr_accel_diff;
    double atr_speed_boost;
    double booster_current;
} nd_refloat_rtdata_t;

typedef struct {
    char name[21];
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    char suffix[21];
} nd_refloat_info_t;

// Parse Refloat COMMAND_GET_ALLDATA response. Returns false on error.
bool nd_refloat_parse_all_data(const uint8_t* data, size_t len, uint8_t mode,
                                nd_refloat_rtdata_t* out);

// Parse Refloat COMMAND_GET_RTDATA response. Returns false on error.
bool nd_refloat_parse_rt_data(const uint8_t* data, size_t len,
                               nd_refloat_rtdata_t* out);

// Parse Refloat COMMAND_INFO response. Returns false on error.
bool nd_refloat_parse_info(const uint8_t* data, size_t len, nd_refloat_info_t* out);

// Build Refloat commands. Caller must free with nd_free(). Sets *out_len.
uint8_t* nd_refloat_build_get_all_data(uint8_t mode, size_t* out_len);
uint8_t* nd_refloat_build_get_rt_data(size_t* out_len);
uint8_t* nd_refloat_build_info_request(size_t* out_len);

// --- BLE Transport ---
// Bridges C++ protocol handling with platform-native BLE stacks.
// The transport handles VESC packet framing, MTU chunking, and reassembly.

typedef struct nd_transport nd_transport_t;

// Callback types (set by platform code)
typedef void (*nd_send_callback_t)(const uint8_t* data, size_t len, void* ctx);
typedef void (*nd_packet_callback_t)(const uint8_t* payload, size_t len, void* ctx);

// Create transport with given BLE MTU (typically 20 for NUS).
nd_transport_t* nd_transport_create(size_t mtu);
void nd_transport_destroy(nd_transport_t* t);

// Set the callback that writes raw bytes to the BLE characteristic.
// ctx is passed through to every callback invocation.
void nd_transport_set_send_callback(nd_transport_t* t, nd_send_callback_t cb, void* ctx);

// Set the callback invoked when a complete VESC packet payload arrives.
void nd_transport_set_packet_callback(nd_transport_t* t, nd_packet_callback_t cb, void* ctx);

// Update MTU after negotiation.
void nd_transport_set_mtu(nd_transport_t* t, size_t mtu);

// Feed raw bytes from a BLE notification. Triggers packet callback if complete.
void nd_transport_receive(nd_transport_t* t, const uint8_t* data, size_t len);

// Send a VESC payload (auto-framed and chunked to MTU).
bool nd_transport_send_payload(nd_transport_t* t, const uint8_t* payload, size_t len);

// Send a single command byte.
bool nd_transport_send_command(nd_transport_t* t, uint8_t cmd);

// Send COMM_CUSTOM_APP_DATA wrapping the given data.
bool nd_transport_send_custom_app_data(nd_transport_t* t, const uint8_t* data, size_t len);

void nd_transport_reset(nd_transport_t* t);

// --- Storage (persistence) ---
// Cross-platform persistence for fleet, profiles, and settings.
// File I/O uses standard paths — caller provides the file path.

typedef struct {
    char id[64];
    char name[128];
    char ble_name[64];
    char ble_address[64];
    int64_t last_connected;
    bool wizard_complete;
    char hw_name[64];
    uint8_t fw_major;
    uint8_t fw_minor;
    char refloat_version[32];
    int motor_pole_pairs;
    double wheel_circumference_m;
    int battery_series_cells;
    double battery_voltage_min;
    double battery_voltage_max;
    double lifetime_distance_m;
    int ride_count;
    char active_profile_id[64];
} nd_board_t;

typedef struct {
    char id[64];
    char name[64];
    char icon[64];
    bool is_built_in;
    int64_t created_at;
    int64_t modified_at;
    double responsiveness;
    double stability;
    double carving;
    double braking;
    double safety;
    double agility;
    double footpad_sensitivity;
    double disengage_speed;
} nd_rider_profile_t;

// Opaque app data handle
typedef struct nd_app_data nd_app_data_t;

// Load persisted data from file. Returns empty data if file missing/corrupt.
nd_app_data_t* nd_app_data_load(const char* path);

// Save persisted data to file. Returns true on success.
bool nd_app_data_save(const nd_app_data_t* data, const char* path);

// Create empty app data.
nd_app_data_t* nd_app_data_create(void);

// Free app data.
void nd_app_data_free(nd_app_data_t* data);

// --- Board accessors ---
size_t nd_app_data_board_count(const nd_app_data_t* data);
bool nd_app_data_get_board(const nd_app_data_t* data, size_t index, nd_board_t* out);
void nd_app_data_set_board(nd_app_data_t* data, size_t index, const nd_board_t* board);
void nd_app_data_add_board(nd_app_data_t* data, const nd_board_t* board);
void nd_app_data_remove_board(nd_app_data_t* data, size_t index);

// --- Rider profile accessors ---
size_t nd_app_data_profile_count(const nd_app_data_t* data);
bool nd_app_data_get_profile(const nd_app_data_t* data, size_t index, nd_rider_profile_t* out);
void nd_app_data_set_profile(nd_app_data_t* data, size_t index, const nd_rider_profile_t* profile);
void nd_app_data_add_profile(nd_app_data_t* data, const nd_rider_profile_t* profile);
void nd_app_data_remove_profile(nd_app_data_t* data, size_t index);

// Active profile ID
const char* nd_app_data_active_profile_id(const nd_app_data_t* data);
void nd_app_data_set_active_profile_id(nd_app_data_t* data, const char* profile_id);

#ifdef __cplusplus
}
#endif
