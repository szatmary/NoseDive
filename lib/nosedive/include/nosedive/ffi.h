#pragma once

// C API for FFI from Swift (direct) and Kotlin (JNI wrapper).
// All functions use C linkage and plain types — no C++ types cross the boundary.
//
// Architecture: C++ Engine owns all business logic. Platform code (Swift/Kotlin)
// creates an engine, feeds it raw VESC payloads, and reads state via getters.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Engine ---
// Opaque handle to the C++ Engine.
typedef struct nd_engine nd_engine_t;

// Callback: engine wants to send a VESC payload (platform must frame + write to transport).
typedef void (*nd_engine_send_cb)(const uint8_t* payload, size_t len, void* ctx);

// Callback: engine state changed (platform should refresh UI).
typedef void (*nd_engine_state_cb)(void* ctx);

// Create engine with storage path. Loads persisted data.
nd_engine_t* nd_engine_create(const char* storage_path);
void nd_engine_destroy(nd_engine_t* e);

// Set callbacks (call before connecting).
void nd_engine_set_send_callback(nd_engine_t* e, nd_engine_send_cb cb, void* ctx);
void nd_engine_set_state_callback(nd_engine_t* e, nd_engine_state_cb cb, void* ctx);

// Connection lifecycle — engine sends discovery commands on connect.
void nd_engine_on_connected(nd_engine_t* e);
void nd_engine_on_disconnected(nd_engine_t* e);

// Feed a raw VESC payload (after packet decoding). Engine dispatches internally.
void nd_engine_handle_payload(nd_engine_t* e, const uint8_t* data, size_t len);

// --- Telemetry (read-only, updated after each COMM_GET_VALUES) ---
typedef struct {
    double temp_mosfet;
    double temp_motor;
    double motor_current;
    double battery_current;
    double duty_cycle;
    double erpm;
    double battery_voltage;
    double battery_percent;
    double speed;           // m/s (computed from board config)
    double power;           // watts
    int32_t tachometer;
    int32_t tachometer_abs;
    uint8_t fault;
} nd_telemetry_t;

void nd_engine_get_telemetry(const nd_engine_t* e, nd_telemetry_t* out);
double nd_engine_speed_kmh(const nd_engine_t* e);
double nd_engine_speed_mph(const nd_engine_t* e);

// --- Active board ---
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

// Returns true if there is an active board (connected).
bool nd_engine_has_active_board(const nd_engine_t* e);
bool nd_engine_get_active_board(const nd_engine_t* e, nd_board_t* out);
bool nd_engine_is_known_board(const nd_engine_t* e);
// Returns NULL if can't guess. Returned string valid until next engine call.
const char* nd_engine_guessed_board_type(const nd_engine_t* e);

// --- CAN devices ---
size_t nd_engine_can_device_count(const nd_engine_t* e);
uint8_t nd_engine_can_device_id(const nd_engine_t* e, size_t index);

// --- Firmware info ---
typedef struct {
    uint8_t major;
    uint8_t minor;
    char hw_name[64];
    char uuid[64];
    uint8_t hw_type;
    uint8_t custom_config_count;
    char package_name[64];
} nd_fw_version_t;

bool nd_engine_get_main_fw(const nd_engine_t* e, nd_fw_version_t* out);

// --- Refloat ---
bool nd_engine_has_refloat(const nd_engine_t* e);

typedef struct {
    char name[21];
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    char suffix[21];
} nd_refloat_info_t;

bool nd_engine_get_refloat_info(const nd_engine_t* e, nd_refloat_info_t* out);
bool nd_engine_refloat_installing(const nd_engine_t* e);
bool nd_engine_refloat_installed(const nd_engine_t* e);
void nd_engine_install_refloat(nd_engine_t* e);

// --- Wizard ---
bool nd_engine_should_show_wizard(const nd_engine_t* e);
void nd_engine_dismiss_wizard(nd_engine_t* e);

// --- Board fleet (persisted) ---
size_t nd_engine_board_count(const nd_engine_t* e);
bool nd_engine_get_board(const nd_engine_t* e, size_t index, nd_board_t* out);
void nd_engine_save_board(nd_engine_t* e, const nd_board_t* board);
void nd_engine_remove_board(nd_engine_t* e, const char* id);

// --- Rider profiles (persisted) ---
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

size_t nd_engine_profile_count(const nd_engine_t* e);
bool nd_engine_get_profile(const nd_engine_t* e, size_t index, nd_rider_profile_t* out);
void nd_engine_save_profile(nd_engine_t* e, const nd_rider_profile_t* profile);
void nd_engine_remove_profile(nd_engine_t* e, const char* id);

const char* nd_engine_active_profile_id(const nd_engine_t* e);
void nd_engine_set_active_profile_id(nd_engine_t* e, const char* profile_id);

// --- Low-level (still useful for BLE transport layer) ---

uint16_t nd_crc16(const uint8_t* data, size_t len);

uint8_t* nd_encode_packet(const uint8_t* payload, size_t payload_len, size_t* out_len);
uint8_t* nd_decode_packet(const uint8_t* data, size_t data_len,
                          size_t* out_len, size_t* consumed);
void nd_free(void* ptr);

// Packet decoder (push-based, for BLE chunk reassembly)
typedef struct nd_decoder nd_decoder_t;
nd_decoder_t* nd_decoder_create(void);
void nd_decoder_destroy(nd_decoder_t* d);
int nd_decoder_feed(nd_decoder_t* d, const uint8_t* data, size_t len);
uint8_t* nd_decoder_pop(nd_decoder_t* d, size_t* out_len);
size_t nd_decoder_count(const nd_decoder_t* d);
void nd_decoder_reset(nd_decoder_t* d);

// BLE transport (handles VESC framing + MTU chunking)
typedef struct nd_transport nd_transport_t;
typedef void (*nd_send_callback_t)(const uint8_t* data, size_t len, void* ctx);
typedef void (*nd_packet_callback_t)(const uint8_t* payload, size_t len, void* ctx);

nd_transport_t* nd_transport_create(size_t mtu);
void nd_transport_destroy(nd_transport_t* t);
void nd_transport_set_send_callback(nd_transport_t* t, nd_send_callback_t cb, void* ctx);
void nd_transport_set_packet_callback(nd_transport_t* t, nd_packet_callback_t cb, void* ctx);
void nd_transport_set_mtu(nd_transport_t* t, size_t mtu);
void nd_transport_receive(nd_transport_t* t, const uint8_t* data, size_t len);
bool nd_transport_send_payload(nd_transport_t* t, const uint8_t* payload, size_t len);
bool nd_transport_send_command(nd_transport_t* t, uint8_t cmd);
bool nd_transport_send_custom_app_data(nd_transport_t* t, const uint8_t* data, size_t len);
void nd_transport_reset(nd_transport_t* t);

#ifdef __cplusplus
}
#endif
