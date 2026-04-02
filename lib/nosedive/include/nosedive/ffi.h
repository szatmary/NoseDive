#pragma once

// C API for FFI from Swift (direct) and Kotlin (JNI wrapper).
// Platform provides raw byte I/O. Engine handles all protocol logic internally.
// Engine pushes parsed state back via domain-specific callbacks (structs by value).

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Engine lifecycle ---
typedef struct nd_engine nd_engine_t;

nd_engine_t* nd_engine_create(const char* storage_path);
void nd_engine_destroy(nd_engine_t* e);

// --- Platform → Engine ---

// Feed raw bytes from BLE/TCP. Engine decodes VESC packets internally.
void nd_engine_receive_bytes(nd_engine_t* e, const uint8_t* data, size_t len);

// Connection lifecycle. MTU is for chunking outgoing packets (use 4096 for TCP).
void nd_engine_on_connected(nd_engine_t* e, size_t mtu);
void nd_engine_on_disconnected(nd_engine_t* e);

// Actions
void nd_engine_install_refloat(nd_engine_t* e);
void nd_engine_dismiss_wizard(nd_engine_t* e);

// --- Engine → Platform: write raw bytes to wire ---
typedef void (*nd_write_cb)(const uint8_t* data, size_t len, void* ctx);
void nd_engine_set_write_callback(nd_engine_t* e, nd_write_cb cb, void* ctx);

// --- Engine → Platform: domain callbacks (structs by value) ---

// Telemetry (fires on each COMM_GET_VALUES response)
typedef struct {
    double temp_mosfet;
    double temp_motor;
    double motor_current;
    double battery_current;
    double duty_cycle;
    double erpm;
    double battery_voltage;
    double battery_percent;
    double speed;
    double power;
    int32_t tachometer;
    int32_t tachometer_abs;
    uint8_t fault;
} nd_telemetry_t;

typedef void (*nd_telemetry_cb)(nd_telemetry_t telemetry, void* ctx);
void nd_engine_set_telemetry_callback(nd_engine_t* e, nd_telemetry_cb cb, void* ctx);

// Board identified (fires on FW_VERSION response)
typedef struct {
    char id[64];
    char name[128];
    char hw_name[64];
    uint8_t fw_major;
    uint8_t fw_minor;
    char uuid[64];
    uint8_t hw_type;
    uint8_t custom_config_count;
    char package_name[64];
    bool show_wizard;
    bool is_known;
} nd_board_event_t;

typedef void (*nd_board_cb)(nd_board_event_t board, void* ctx);
void nd_engine_set_board_callback(nd_engine_t* e, nd_board_cb cb, void* ctx);

// Refloat state changed
typedef struct {
    bool has_refloat;
    char name[21];
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    char suffix[21];
    bool installing;
    bool installed;
} nd_refloat_event_t;

typedef void (*nd_refloat_cb)(nd_refloat_event_t info, void* ctx);
void nd_engine_set_refloat_callback(nd_engine_t* e, nd_refloat_cb cb, void* ctx);

// CAN devices discovered
typedef void (*nd_can_cb)(const uint8_t* ids, size_t count, void* ctx);
void nd_engine_set_can_callback(nd_engine_t* e, nd_can_cb cb, void* ctx);

// Diagnostic errors
typedef void (*nd_error_cb)(const char* message, void* ctx);
void nd_engine_set_error_callback(nd_engine_t* e, nd_error_cb cb, void* ctx);

// --- Setup ---
typedef enum {
    ND_SETUP_IDLE,
    ND_SETUP_CHECK_FW_EXPRESS,
    ND_SETUP_CHECK_FW_BMS,
    ND_SETUP_CHECK_FW_VESC,
    ND_SETUP_INSTALL_REFLOAT,
    ND_SETUP_DETECT_BATTERY,
    ND_SETUP_DETECT_FOOTPADS,
    ND_SETUP_CALIBRATE_IMU,
    ND_SETUP_DETECT_MOTOR,
    ND_SETUP_CONFIGURE_WHEEL,
    ND_SETUP_DONE,
} nd_setup_step_t;

typedef struct {
    nd_setup_step_t step;
    char error[128];
    char detail[256];
} nd_setup_state_t;

typedef void (*nd_setup_cb)(nd_setup_state_t state, void* ctx);
void nd_engine_set_setup_callback(nd_engine_t* e, nd_setup_cb cb, void* ctx);
void nd_engine_setup_start(nd_engine_t* e);
void nd_engine_setup_retry(nd_engine_t* e);
void nd_engine_setup_skip(nd_engine_t* e);
void nd_engine_setup_abort(nd_engine_t* e);

// --- Board fleet (persisted) ---
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

size_t nd_engine_board_count(const nd_engine_t* e);
nd_board_t nd_engine_get_board(const nd_engine_t* e, size_t index);
void nd_engine_save_board(nd_engine_t* e, nd_board_t board);
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
nd_rider_profile_t nd_engine_get_profile(const nd_engine_t* e, size_t index);
void nd_engine_save_profile(nd_engine_t* e, nd_rider_profile_t profile);
void nd_engine_remove_profile(nd_engine_t* e, const char* id);

const char* nd_engine_active_profile_id(const nd_engine_t* e);
void nd_engine_set_active_profile_id(nd_engine_t* e, const char* profile_id);

#ifdef __cplusplus
}
#endif
