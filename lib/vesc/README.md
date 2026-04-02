# libvesc

C++ library for the VESC protocol. Handles packet framing, command encoding/decoding, and connection management. No platform dependencies — pure C++17.

## Usage

### Connection

```cpp
#include <vesc/vesc.hpp>

vesc::Connection conn;

// Platform writes framed bytes to BLE/TCP
conn.set_write_callback([](const uint8_t* data, size_t len) {
    my_ble_write(data, len);  // or tcp_send(data, len)
});

// Engine receives decoded payloads
conn.set_payload_callback([](const uint8_t* payload, size_t len) {
    auto cmd = static_cast<vesc::CommPacketID>(payload[0]);
    // dispatch based on cmd...
});

// When bytes arrive from BLE/TCP
void on_ble_data(const uint8_t* data, size_t len) {
    conn.receive(data, len);  // decodes internally, fires payload callback
}

// On disconnect
conn.reset();
```

### Sending Commands

Every command has a `Request` with an `encode()` method that returns the payload bytes:

```cpp
// Simple command (just the ID byte)
auto payload = vesc::GetValues::Request{}.encode();
conn.send(payload);

// Command with parameters
auto payload = vesc::GetIMUData::Request{.mask = 0x001F}.encode();
conn.send(payload);

// Command with complex parameters
vesc::DetectApplyAllFOC::Request req;
req.detect_can = false;
req.max_power_loss = 1.0;
conn.send(req.encode());

// Forward a command to a CAN device
vesc::ForwardCAN::Request req;
req.target_id = 253;
req.inner_payload = vesc::FWVersion::Request{}.encode();
conn.send(req.encode());
```

### Receiving Responses

Every command has a `Response` with a static `decode()` that returns `std::optional<Response>`:

```cpp
conn.set_payload_callback([](const uint8_t* data, size_t len) {
    auto cmd = static_cast<vesc::CommPacketID>(data[0]);

    switch (cmd) {
    case vesc::FWVersion::id: {
        if (auto fw = vesc::FWVersion::Response::decode(data, len)) {
            printf("FW %d.%d hw=%s\n", fw->major, fw->minor, fw->hw_name.c_str());
        }
        break;
    }
    case vesc::GetValues::id: {
        if (auto v = vesc::GetValues::Response::decode(data, len)) {
            printf("%.1fV %.1fA %.0fRPM\n", v->voltage, v->avg_motor_current, v->rpm);
        }
        break;
    }
    case vesc::PingCAN::id: {
        if (auto r = vesc::PingCAN::Response::decode(data, len)) {
            for (auto id : r->device_ids) printf("CAN device: %d\n", id);
        }
        break;
    }
    }
});
```

### Refloat

Refloat commands go through `CustomAppData`:

```cpp
// Request Refloat info
auto payload = vesc::build_refloat_info_request();
conn.send(payload);

// Parse Refloat info response
case vesc::CustomAppData::id: {
    if (auto info = vesc::parse_refloat_info(data, len)) {
        printf("Refloat %s\n", info->version_string().c_str());
    }
    break;
}
```

## Commands

| Command | ID | Request params | Response fields |
|---------|-----|---------------|-----------------|
| `FWVersion` | 0x00 | — | major, minor, hw_name, uuid, hw_type, package_name |
| `GetValues` | 0x04 | — | voltage, current, temp, RPM, duty, fault, ... |
| `GetValuesSetup` | 0x2F | — | aggregated values, battery level, distance, uptime |
| `Alive` | 0x1E | — | (no response) |
| `PingCAN` | 0x3E | — | device_ids[] |
| `ForwardCAN` | 0x22 | target_id, inner_payload | (inner response) |
| `GetIMUData` | 0x41 | mask | roll, pitch, yaw, accel, gyro, mag, quaternion |
| `GetBatteryCut` | 0x73 | — | voltage_start, voltage_end |
| `DetectApplyAllFOC` | 0x3A | detect_can, max_power_loss, ... | result (0=success) |
| `GetStats` | 0x80 | mask | speed/power/current avg/max, temps, uptime |
| `CustomAppData` | 0x24 | payload (app-specific) | (app-specific) |
| `GetCustomConfigXML` | 0x5C | config_index, len, offset | total_size, offset, data |
| `GetQMLUIApp` | 0x76 | len, offset | total_size, offset, data |

## Building

```bash
cd lib/vesc
cmake -B build
cmake --build build
./build/vesc_tests
```

## Integration

libvesc is a static library with no dependencies. Add it to your CMake project:

```cmake
add_subdirectory(lib/vesc)
target_link_libraries(your_target PRIVATE vesc)
```

Include the umbrella header:

```cpp
#include <vesc/vesc.hpp>
```
