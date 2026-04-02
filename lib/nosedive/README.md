# libnosedive

C++ application engine for NoseDive. Owns all business logic — board discovery, Refloat integration, fleet management, rider profiles. Platform code (Swift/Kotlin) feeds raw bytes in and receives parsed state via callbacks.

Depends on [libvesc](../vesc/) for VESC protocol handling.

## Architecture

```
Platform (Swift/Kotlin)
  │
  ├── Raw bytes from BLE/TCP ──→ nd_engine_receive_bytes()
  │                                   │
  │                              Engine (C++)
  │                              ├── vesc::Connection (decode/encode)
  │                              ├── Command dispatch
  │                              ├── State management
  │                              ├── Board fleet (Storage)
  │                              └── Rider profiles
  │
  ├── nd_telemetry_cb  ←── Telemetry update
  ├── nd_board_cb      ←── Board identified / wizard
  ├── nd_refloat_cb    ←── Refloat state changed
  ├── nd_can_cb        ←── CAN devices discovered
  ├── nd_error_cb      ←── Diagnostic errors
  │
  └── nd_write_cb      ←── Engine sends framed bytes to wire
```

## FFI (C API)

The engine is used from Swift and Kotlin via a C FFI (`ffi.h`). No C++ types cross the boundary — only plain C structs passed by value.

### Setup

```c
// Create engine with storage path for persistence
nd_engine_t* engine = nd_engine_create("/path/to/storage.json");

// Set write callback — engine sends raw bytes here, platform writes to BLE/TCP
nd_engine_set_write_callback(engine, my_write_fn, ctx);

// Set domain callbacks — engine pushes parsed state
nd_engine_set_telemetry_callback(engine, my_telemetry_fn, ctx);
nd_engine_set_board_callback(engine, my_board_fn, ctx);
nd_engine_set_refloat_callback(engine, my_refloat_fn, ctx);
nd_engine_set_can_callback(engine, my_can_fn, ctx);
nd_engine_set_error_callback(engine, my_error_fn, ctx);
```

### Connection lifecycle

```c
// When BLE/TCP connects — engine sends discovery commands
nd_engine_on_connected(engine, 512);  // MTU for chunking outgoing writes

// Feed raw bytes from BLE/TCP — engine decodes and dispatches internally
nd_engine_receive_bytes(engine, data, len);

// When disconnected
nd_engine_on_disconnected(engine);
```

### Callbacks

All callbacks deliver structs by value. No pointers to manage.

```c
// Telemetry — fires on each COMM_GET_VALUES response
void on_telemetry(nd_telemetry_t t, void* ctx) {
    printf("%.1fV %.1fA %.0fRPM\n", t.battery_voltage, t.motor_current, t.erpm);
}

// Board identified — fires on COMM_FW_VERSION response
void on_board(nd_board_event_t board, void* ctx) {
    printf("Board: %s FW %d.%02d\n", board.hw_name, board.fw_major, board.fw_minor);
    if (board.show_wizard) {
        // Unknown board — show setup wizard
    }
}

// Refloat state — fires when Refloat info changes
void on_refloat(nd_refloat_event_t info, void* ctx) {
    if (info.has_refloat) {
        printf("Refloat %d.%d.%d\n", info.major, info.minor, info.patch);
    }
}

// CAN devices — fires after CAN scan completes
void on_can(const uint8_t* ids, size_t count, void* ctx) {
    for (size_t i = 0; i < count; i++) printf("CAN device: %d\n", ids[i]);
}
```

### Actions

```c
nd_engine_install_refloat(engine);   // Start Refloat installation
nd_engine_dismiss_wizard(engine);    // User dismissed the setup wizard
```

### Board fleet (persisted)

```c
size_t count = nd_engine_board_count(engine);
nd_board_t board = nd_engine_get_board(engine, 0);

board.wizard_complete = true;
board.battery_series_cells = 21;
nd_engine_save_board(engine, board);

nd_engine_remove_board(engine, "board-id");
```

### Rider profiles (persisted)

```c
size_t count = nd_engine_profile_count(engine);
nd_rider_profile_t profile = nd_engine_get_profile(engine, 0);

nd_engine_save_profile(engine, profile);
nd_engine_remove_profile(engine, "profile-id");

nd_engine_set_active_profile_id(engine, "profile-id");
const char* active = nd_engine_active_profile_id(engine);
```

### Cleanup

```c
nd_engine_destroy(engine);
```

## Callback structs

### nd_telemetry_t
| Field | Type | Description |
|-------|------|-------------|
| temp_mosfet | double | MOSFET temperature (C) |
| temp_motor | double | Motor temperature (C) |
| motor_current | double | Motor current (A) |
| battery_current | double | Battery current (A) |
| duty_cycle | double | Duty cycle (0-1) |
| erpm | double | Electrical RPM |
| battery_voltage | double | Battery voltage (V) |
| battery_percent | double | Battery level (0-100) |
| speed | double | Speed (m/s) |
| power | double | Power (W) |
| tachometer | int32_t | Tachometer value |
| fault | uint8_t | Fault code (0=none) |

### nd_board_event_t
| Field | Type | Description |
|-------|------|-------------|
| id | char[64] | Board UUID |
| name | char[128] | Board name |
| hw_name | char[64] | Hardware name (e.g. "60_MK6") |
| fw_major/minor | uint8_t | Firmware version |
| uuid | char[64] | UUID hex string |
| hw_type | uint8_t | 0=VESC, 3=Express |
| custom_config_count | uint8_t | Number of custom configs |
| package_name | char[64] | Package name (e.g. "Refloat") |
| show_wizard | bool | True if board is unknown — show setup |
| is_known | bool | True if board is in the fleet |

### nd_refloat_event_t
| Field | Type | Description |
|-------|------|-------------|
| has_refloat | bool | Refloat is installed |
| name | char[21] | Package name |
| major/minor/patch | uint8_t | Version |
| suffix | char[21] | Version suffix |
| installing | bool | Install in progress |
| installed | bool | Install completed |

## Building

```bash
cd lib/nosedive
cmake -B build
cmake --build build
./build/nosedive_tests
```

Requires libvesc to be available at `../vesc/`.

## Project structure

```
lib/nosedive/
  include/nosedive/
    ffi.h           C API for Swift/Kotlin
    engine.hpp      Application engine
    storage.hpp     Board fleet + rider profiles (JSON persistence)
    profile.hpp     Board hardware profile (from JSON)
    nosedive.hpp    Umbrella header
  src/
    ffi.cpp         C API implementation
    engine.cpp      Engine logic
    storage.cpp     JSON persistence
    profile.cpp     Profile parser
  tests/
    test_main.cpp   Test suite
```
