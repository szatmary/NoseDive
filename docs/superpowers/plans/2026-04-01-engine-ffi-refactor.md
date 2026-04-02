# Engine FFI Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Engine owns all protocol logic internally. Platform code only provides raw byte I/O and receives parsed structs via callbacks. No pointers cross the FFI boundary except raw byte buffers for I/O.

**Architecture:** Engine absorbs the PacketDecoder/encoder. Platform calls `nd_engine_receive_bytes()` with raw wire data. Engine decodes VESC packets, processes commands, and fires domain-specific callbacks (telemetry, board, refloat, etc.) with structs by value. The `nd_transport_t` FFI type is removed entirely.

**Tech Stack:** C++17, Swift (iOS/macOS), Kotlin/JNI (Android), CMake

---

## File Map

### Delete
- `lib/nosedive/include/nosedive/ble_transport.hpp`
- `lib/nosedive/src/ble_transport.cpp`

### Modify
- `lib/nosedive/include/nosedive/engine.hpp` — Engine owns decoder, new callback types, remove getters
- `lib/nosedive/src/engine.cpp` — Absorb packet decode/encode, fire domain callbacks
- `lib/nosedive/include/nosedive/ffi.h` — New FFI surface: receive_bytes, write_cb, domain callbacks. Remove transport, getters
- `lib/nosedive/src/ffi.cpp` — Implement new FFI, remove transport/getter wrappers
- `lib/nosedive/tests/test_main.cpp` — Update tests for new API
- `lib/nosedive/CMakeLists.txt` — Remove ble_transport.cpp
- `ios/NoseDive/Sources/CNoseDive/nosedive_ffi.h` — Symlink updates automatically
- `ios/NoseDive/Sources/ViewModels/BoardManager.swift` — Use new callbacks, remove transport setup
- `ios/NoseDive/Sources/Services/TCPTransport.swift` — Simplify to just raw byte I/O
- `android/app/src/main/cpp/nosedive_jni.cpp` — Use new FFI

### Keep (no changes)
- `lib/nosedive/include/nosedive/protocol.hpp` — PacketDecoder/encoder stay, engine uses them internally
- `lib/nosedive/src/protocol.cpp` — No changes
- `lib/nosedive/include/nosedive/commands.hpp` — Struct definitions used by callbacks
- `ios/NoseDive/Sources/Services/BLEService.swift` — Already just raw byte I/O
- `ios/NoseDive/Sources/Models/BoardState.swift` — Swift-side structs unchanged

---

### Task 1: Remove BLETransport, engine absorbs decoder

**Files:**
- Delete: `lib/nosedive/include/nosedive/ble_transport.hpp`
- Delete: `lib/nosedive/src/ble_transport.cpp`
- Modify: `lib/nosedive/include/nosedive/engine.hpp`
- Modify: `lib/nosedive/src/engine.cpp`
- Modify: `lib/nosedive/include/nosedive/nosedive.hpp`
- Modify: `lib/nosedive/CMakeLists.txt`

- [ ] **Step 1: Remove BLETransport from nosedive.hpp**

In `lib/nosedive/include/nosedive/nosedive.hpp`, remove the include:
```cpp
// Remove this line:
// #include "nosedive/ble_transport.hpp"
```

- [ ] **Step 2: Add PacketDecoder to Engine**

In `lib/nosedive/include/nosedive/engine.hpp`, add:
```cpp
#include "nosedive/protocol.hpp"
```

Add to private members:
```cpp
    PacketDecoder decoder_;
    size_t mtu_ = 512;
```

Replace `SendCallback` signature — engine now writes raw framed bytes, not payloads:
```cpp
/// Callback to write raw bytes to the wire (BLE/TCP). Platform implements this.
using WriteCallback = std::function<void(const uint8_t* data, size_t len)>;
```

Replace `send_cb_` with `write_cb_`:
```cpp
    WriteCallback write_cb_;
```

Change public API:
```cpp
    void set_write_callback(WriteCallback cb);
    void receive_bytes(const uint8_t* data, size_t len);
    void on_connected(size_t mtu = 512);
```

Remove:
```cpp
    void handle_payload(const uint8_t* data, size_t len);
    void set_send_callback(SendCallback cb);
```

- [ ] **Step 3: Implement receive_bytes and internal send**

In `lib/nosedive/src/engine.cpp`:

Replace `set_send_callback` with:
```cpp
void Engine::set_write_callback(WriteCallback cb) {
    std::lock_guard lock(mu_);
    write_cb_ = std::move(cb);
}
```

Add `receive_bytes`:
```cpp
void Engine::receive_bytes(const uint8_t* data, size_t len) {
    decoder_.feed(data, len);
    while (decoder_.has_packet()) {
        auto payload = decoder_.pop();
        if (!payload.empty()) {
            handle_payload(payload.data(), payload.size());
        }
    }
}
```

Make `handle_payload` private (already declared private in header).

Change `on_connected` to accept MTU:
```cpp
void Engine::on_connected(size_t mtu) {
    std::unique_lock lock(mu_);
    mtu_ = mtu;
    decoder_.reset();
    // Discovery sequence
    send_command(CommPacketID::FWVersion);
    send_command(CommPacketID::PingCAN);
    send_refloat_info_request();
    flush_pending(lock);
}
```

Replace `queue_send` with internal encode+chunk+write:
```cpp
void Engine::queue_send(const std::vector<uint8_t>& payload) {
    auto pkt = encode_packet(payload.data(), payload.size());
    if (pkt.empty()) return;
    pending_sends_.push_back(std::move(pkt));
}
```

Update `flush_pending` to write raw bytes:
```cpp
void Engine::flush_pending(std::unique_lock<std::mutex>& lock) {
    auto sends = std::move(pending_sends_);
    pending_sends_.clear();
    // ... (keep error and notify handling)
    auto write_cb = write_cb_;
    lock.unlock();

    for (auto& pkt : sends) {
        if (write_cb) {
            // Chunk to MTU
            size_t offset = 0;
            while (offset < pkt.size()) {
                size_t chunk = std::min(mtu_, pkt.size() - offset);
                write_cb(pkt.data() + offset, chunk);
                offset += chunk;
            }
        }
    }
    // ... (keep notify and error callbacks)
}
```

Add helpers:
```cpp
void Engine::send_command(CommPacketID cmd) {
    uint8_t c = static_cast<uint8_t>(cmd);
    queue_send({c});
}

void Engine::send_refloat_info_request() {
    queue_send(build_refloat_info_request());
}
```

- [ ] **Step 4: Remove ble_transport files from CMakeLists.txt**

In `lib/nosedive/CMakeLists.txt`, remove `src/ble_transport.cpp` from all source lists.

- [ ] **Step 5: Delete BLETransport files**

```bash
rm lib/nosedive/include/nosedive/ble_transport.hpp
rm lib/nosedive/src/ble_transport.cpp
```

- [ ] **Step 6: Build and run C++ tests**

```bash
cd lib/nosedive && rm -rf build && cmake -B build && cmake --build build --parallel
cd build && ./nosedive_tests
```

Fix any compilation errors. Tests that used BLETransport or nd_transport need updating (handled in Task 3).

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "Remove BLETransport, engine owns packet decoder and encoder internally"
```

---

### Task 2: Replace FFI with new callbacks

**Files:**
- Modify: `lib/nosedive/include/nosedive/ffi.h`
- Modify: `lib/nosedive/src/ffi.cpp`

- [ ] **Step 1: Rewrite ffi.h**

Replace the entire file with:

```c
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Engine ---
typedef struct nd_engine nd_engine_t;

nd_engine_t* nd_engine_create(const char* storage_path);
void nd_engine_destroy(nd_engine_t* e);

// --- Platform → Engine ---

// Feed raw bytes from BLE/TCP. Engine decodes VESC packets internally.
void nd_engine_receive_bytes(nd_engine_t* e, const uint8_t* data, size_t len);

// Connection lifecycle. MTU is used for chunking outgoing packets.
void nd_engine_on_connected(nd_engine_t* e, size_t mtu);
void nd_engine_on_disconnected(nd_engine_t* e);

// Actions
void nd_engine_install_refloat(nd_engine_t* e);
void nd_engine_dismiss_wizard(nd_engine_t* e);

// --- Engine → Platform: write bytes to wire ---
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

// Refloat state (fires when Refloat info changes)
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

// CAN devices discovered (fires after CAN scan completes)
typedef void (*nd_can_cb)(const uint8_t* ids, size_t count, void* ctx);
void nd_engine_set_can_callback(nd_engine_t* e, nd_can_cb cb, void* ctx);

// Diagnostic errors
typedef void (*nd_error_cb)(const char* message, void* ctx);
void nd_engine_set_error_callback(nd_engine_t* e, nd_error_cb cb, void* ctx);

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
```

- [ ] **Step 2: Rewrite ffi.cpp**

Remove all `nd_transport_*` functions and all getter functions (`nd_engine_get_telemetry`, `nd_engine_get_active_board`, `nd_engine_get_main_fw`, `nd_engine_has_refloat`, `nd_engine_get_refloat_info`, `nd_engine_should_show_wizard`, etc.).

Update `nd_engine` struct to hold the new callback pointers:
```cpp
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
```

Implement new FFI functions:
```cpp
void nd_engine_receive_bytes(nd_engine_t* e, const uint8_t* data, size_t len) {
    e->engine.receive_bytes(data, len);
}

void nd_engine_on_connected(nd_engine_t* e, size_t mtu) {
    e->engine.on_connected(mtu);
}

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
        nd_telemetry_t ct = {};
        ct.temp_mosfet = t.temp_mosfet;
        ct.temp_motor = t.temp_motor;
        ct.motor_current = t.motor_current;
        ct.battery_current = t.battery_current;
        ct.duty_cycle = t.duty_cycle;
        ct.erpm = t.erpm;
        ct.battery_voltage = t.battery_voltage;
        ct.battery_percent = t.battery_percent;
        ct.speed = t.speed;
        ct.power = t.power;
        ct.tachometer = t.tachometer;
        ct.tachometer_abs = t.tachometer_abs;
        ct.fault = t.fault;
        e->telemetry_cb(ct, e->telemetry_ctx);
    });
}
// ... similar for board, refloat, can, error callbacks
```

- [ ] **Step 3: Build C++ library**

```bash
cd lib/nosedive && cmake --build build --parallel
```

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "Replace FFI getters and transport with domain callbacks"
```

---

### Task 3: Add domain callbacks to Engine

**Files:**
- Modify: `lib/nosedive/include/nosedive/engine.hpp`
- Modify: `lib/nosedive/src/engine.cpp`

- [ ] **Step 1: Add callback types to Engine**

In `engine.hpp`, add new callback types:
```cpp
using TelemetryCallback = std::function<void(const Telemetry&)>;
using BoardCallback = std::function<void(const Board&, const FWVersion&, bool show_wizard, bool is_known)>;
using RefloatCallback = std::function<void(bool has_refloat, const std::optional<RefloatInfo>&, bool installing, bool installed)>;
using CANCallback = std::function<void(const std::vector<uint8_t>& ids)>;
```

Add setters and private members:
```cpp
    void set_telemetry_callback(TelemetryCallback cb);
    void set_board_callback(BoardCallback cb);
    void set_refloat_callback(RefloatCallback cb);
    void set_can_callback(CANCallback cb);
```

```cpp
    TelemetryCallback telemetry_cb_;
    BoardCallback board_cb_;
    RefloatCallback refloat_cb_;
    CANCallback can_cb_;
```

Remove `StateCallback` and `set_state_callback`. Remove `queue_notify` and `pending_notify_`.

- [ ] **Step 2: Fire callbacks from handlers**

In `engine.cpp`, replace `queue_notify()` calls with specific callback fires:

In `handle_values` — after updating telemetry:
```cpp
    // Queue telemetry callback
    pending_telemetry_ = true;
```

In `handle_fw_info` — after setting active_board:
```cpp
    // Queue board callback
    pending_board_ = true;
```

In `handle_custom_app_data` — when refloat info changes:
```cpp
    // Queue refloat callback
    pending_refloat_ = true;
```

In `handle_ping_can` — after updating CAN list:
```cpp
    // Queue CAN callback
    pending_can_ = true;
```

Update `flush_pending` to fire the specific callbacks:
```cpp
void Engine::flush_pending(std::unique_lock<std::mutex>& lock) {
    auto sends = std::move(pending_sends_);
    pending_sends_.clear();
    auto errors = std::move(pending_errors_);
    pending_errors_.clear();

    bool fire_telemetry = pending_telemetry_;
    bool fire_board = pending_board_;
    bool fire_refloat = pending_refloat_;
    bool fire_can = pending_can_;
    pending_telemetry_ = pending_board_ = pending_refloat_ = pending_can_ = false;

    // Snapshot state for callbacks
    auto telemetry = telemetry_;
    auto board = active_board_;
    auto fw = main_fw_;
    bool wizard = show_wizard_;
    bool known = is_known_board_locked();
    auto refloat = refloat_info_;
    bool has_rf = refloat_info_.has_value();
    bool rf_installing = refloat_installing_;
    bool rf_installed = refloat_installed_;
    auto can_ids = can_device_ids_;

    // Snapshot callbacks
    auto write_cb = write_cb_;
    auto telemetry_cb = telemetry_cb_;
    auto board_cb = board_cb_;
    auto refloat_cb = refloat_cb_;
    auto can_cb = can_cb_;
    auto error_cb = error_cb_;

    lock.unlock();

    // Write outgoing packets
    for (auto& pkt : sends) {
        if (write_cb) {
            size_t offset = 0;
            while (offset < pkt.size()) {
                size_t chunk = std::min(mtu_, pkt.size() - offset);
                write_cb(pkt.data() + offset, chunk);
                offset += chunk;
            }
        }
    }

    // Fire domain callbacks
    if (fire_telemetry && telemetry_cb) telemetry_cb(telemetry);
    if (fire_board && board_cb && board && fw) board_cb(*board, *fw, wizard, known);
    if (fire_refloat && refloat_cb) refloat_cb(has_rf, refloat, rf_installing, rf_installed);
    if (fire_can && can_cb) can_cb(can_ids);
    for (auto& msg : errors) {
        if (error_cb) error_cb(msg.c_str());
    }
}
```

- [ ] **Step 3: Build and test**

```bash
cd lib/nosedive && cmake --build build --parallel && cd build && ./nosedive_tests
```

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "Engine fires domain-specific callbacks instead of generic state notification"
```

---

### Task 4: Update tests

**Files:**
- Modify: `lib/nosedive/tests/test_main.cpp`

- [ ] **Step 1: Remove BLETransport and nd_transport tests**

Remove `test_ble_transport`, `test_ffi_transport`, and any test that references `BLETransport` or `nd_transport_*`.

- [ ] **Step 2: Add engine receive_bytes test**

```cpp
static void test_engine_receive_bytes() {
    nd_engine_t* e = nd_engine_create("/tmp/test_engine_rx");

    bool got_board = false;
    nd_board_event_t last_board = {};
    nd_engine_set_board_callback(e, [](nd_board_event_t board, void* ctx) {
        *(bool*)ctx = true;
    }, &got_board);

    // Build a FW_VERSION response packet (framed)
    uint8_t payload[] = {0x00, 6, 5}; // COMM_FW_VERSION, major=6, minor=5
    // ... add hw_name, uuid, etc to make a valid response
    auto pkt = nosedive::encode_packet(payload, sizeof(payload));

    nd_engine_on_connected(e, 512);
    nd_engine_receive_bytes(e, pkt.data(), pkt.size());

    ASSERT(got_board, "board callback fired after FW_VERSION");

    nd_engine_destroy(e);
}
```

- [ ] **Step 3: Build and run tests**

```bash
cd lib/nosedive && cmake --build build --parallel && cd build && ./nosedive_tests
```

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "Update tests for engine receive_bytes and domain callbacks"
```

---

### Task 5: Update iOS app (BoardManager.swift)

**Files:**
- Modify: `ios/NoseDive/Sources/ViewModels/BoardManager.swift`
- Modify: `ios/NoseDive/Sources/Services/TCPTransport.swift`
- Modify: `ios/NoseDive/Sources/CNoseDive/nosedive_ffi.h` (symlink — auto-updates)

- [ ] **Step 1: Remove transport setup from BoardManager**

Remove `setupVESCTransport`, `teardownVESCTransport`, `vescTransport` property, `feedTransport` method, and all `nd_transport_*` calls.

- [ ] **Step 2: Wire domain callbacks in init**

Replace the state callback setup with domain callbacks:
```swift
init() {
    engine = nd_engine_create(storagePath)
    let selfPtr = Unmanaged.passUnretained(self).toOpaque()

    // Write callback — send raw bytes to BLE/TCP
    nd_engine_set_write_callback(engine, { data, len, ctx in
        guard let ctx, let data else { return }
        let chunk = Data(bytes: data, count: len)
        let mgr = Unmanaged<BoardManager>.fromOpaque(ctx).takeUnretainedValue()
        Task { @MainActor in
            switch mgr.activeTransport {
            case .ble:  mgr.bleService?.send(chunk)
            case .tcp:  mgr.tcpTransport?.send(chunk)
            case .none: break
            }
        }
    }, selfPtr)

    // Telemetry callback
    nd_engine_set_telemetry_callback(engine, { t, ctx in
        guard let ctx else { return }
        let mgr = Unmanaged<BoardManager>.fromOpaque(ctx).takeUnretainedValue()
        Task { @MainActor in
            mgr.telemetry = BoardTelemetry(
                speed: t.speed,
                dutyCycle: t.duty_cycle,
                batteryVoltage: t.battery_voltage,
                batteryPercent: t.battery_percent,
                motorCurrent: t.motor_current,
                batteryCurrent: t.battery_current,
                power: t.power,
                mosfetTemp: t.temp_mosfet,
                motorTemp: t.temp_motor,
                erpm: t.erpm,
                tachometer: t.tachometer,
                tachometerAbs: t.tachometer_abs,
                fault: t.fault
            )
        }
    }, selfPtr)

    // Board callback
    nd_engine_set_board_callback(engine, { board, ctx in
        guard let ctx else { return }
        let mgr = Unmanaged<BoardManager>.fromOpaque(ctx).takeUnretainedValue()
        Task { @MainActor in
            mgr.mainFWInfo = FWVersionInfo(
                hwName: cString(board.hw_name),
                major: board.fw_major,
                minor: board.fw_minor,
                uuid: cString(board.uuid),
                hwType: board.hw_type,
                customConfigCount: board.custom_config_count,
                packageName: cString(board.package_name)
            )
            mgr.showWizard = board.show_wizard
        }
    }, selfPtr)

    // Refloat callback
    nd_engine_set_refloat_callback(engine, { info, ctx in
        guard let ctx else { return }
        let mgr = Unmanaged<BoardManager>.fromOpaque(ctx).takeUnretainedValue()
        Task { @MainActor in
            if info.has_refloat {
                mgr.refloatInfo = RefloatInfo(
                    name: cString(info.name),
                    major: info.major,
                    minor: info.minor,
                    patch: info.patch,
                    suffix: cString(info.suffix)
                )
            } else {
                mgr.refloatInfo = nil
            }
            mgr.refloatInstalling = info.installing
            mgr.refloatInstalled = info.installed
        }
    }, selfPtr)

    // Error callback
    nd_engine_set_error_callback(engine, { message, ctx in
        guard let message else { return }
        print("NoseDive engine: \(String(cString: message))")
    }, selfPtr)
}
```

- [ ] **Step 3: Simplify connect methods**

```swift
func connectTCP(host: String, port: UInt16) {
    disconnect()
    connectionState = .connecting("\(host):\(port)")
    activeTransport = .tcp

    let transport = TCPTransport { [weak self] event in
        Task { @MainActor in
            self?.handleTCPEvent(event)
        }
    }
    tcpTransport = transport
    transport.connect(host: host, port: port)
}

private func handleTCPEvent(_ event: TCPTransport.Event) {
    switch event {
    case .connected:
        connectionState = .connected
        nd_engine_on_connected(engine, 4096)
    case .disconnected:
        connectionState = .disconnected
        activeTransport = .none
        tcpTransport = nil
        nd_engine_on_disconnected(engine)
    case .data(let rawData):
        rawData.withUnsafeBytes { buf in
            guard let ptr = buf.baseAddress?.assumingMemoryBound(to: UInt8.self) else { return }
            nd_engine_receive_bytes(engine, ptr, buf.count)
        }
    }
}
```

Similarly for BLE — `handleBLEEvent(.data)` calls `nd_engine_receive_bytes` directly.

- [ ] **Step 4: Remove refreshFromEngine**

Delete `refreshFromEngine()` entirely — all state updates come via callbacks now.

- [ ] **Step 5: Build and test**

```bash
cd ios/NoseDive && swift build
```

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "iOS: use domain callbacks, remove transport layer from Swift"
```

---

### Task 6: Update Android JNI

**Files:**
- Modify: `android/app/src/main/cpp/nosedive_jni.cpp`

- [ ] **Step 1: Update JNI to use new FFI**

Replace `nd_transport_*` calls with `nd_engine_receive_bytes`. Replace getter calls with callback wiring. Follow same pattern as iOS — JNI callbacks fire back to Kotlin.

- [ ] **Step 2: Commit**

```bash
git add -A
git commit -m "Android: use domain callbacks, remove transport from JNI"
```

---

### Task 7: Cleanup and final verification

**Files:**
- All modified files

- [ ] **Step 1: Full C++ test suite**

```bash
cd lib/nosedive && rm -rf build && cmake -B build && cmake --build build --parallel
cd build && ./nosedive_tests
```

- [ ] **Step 2: iOS build**

```bash
cd ios/NoseDive && rm -rf .build && swift build
```

- [ ] **Step 3: Run app against simulator**

```bash
# Terminal 1
cd /path/to/NoseDive && ./vescsim-bin -ble -web -fresh

# Terminal 2
cd /path/to/NoseDive && ./run.sh
```

Connect via "Connect Simulator" button. Verify:
- Board identified callback fires
- Wizard appears for unknown board
- Telemetry updates when simulator sends values

- [ ] **Step 4: Remove debug logging**

Remove all `fopen("/tmp/engine_debug.log"...)` calls from engine.cpp and ffi.cpp.

- [ ] **Step 5: Final commit**

```bash
git add -A
git commit -m "Remove debug logging, cleanup complete"
```
