# NoseDive Protocol Reference

This document describes all VESC and Refloat endpoints implemented by the NoseDive simulator, the CLI commands available, and configuration options.

## Table of Contents

- [CLI Usage](#cli-usage)
- [Transport Layer](#transport-layer)
- [VESC Packet Framing](#vesc-packet-framing)
- [VESC Commands](#vesc-commands)
- [Refloat Commands](#refloat-commands)
- [Simulator Configuration](#simulator-configuration)

---

## CLI Usage

```
nosedive [flags]
```

### Connection Flags

| Flag | Description |
|------|-------------|
| `--sim` | Start with built-in simulator (TCP) |
| `--sim-addr HOST:PORT` | Simulator listen address (default `127.0.0.1:0`) |
| `--sim-ble` | Enable BLE on simulator (emulates VESC Express) |
| `--ble-name NAME` | BLE device name for simulator (default `VESC SIM`) |
| `--addr HOST:PORT` | Connect to a real VESC over TCP |
| `--ble-scan` | Scan for VESC BLE devices (5 seconds) |
| `--ble XX:XX:XX:XX:XX:XX` | Connect to VESC via BLE address |

### Interactive Commands

#### VESC / Refloat

| Command | Aliases | Description |
|---------|---------|-------------|
| `fw` | `firmware` | Show VESC firmware version |
| `info` | `i` | Show Refloat package info (name, version) |
| `values` | `v` | Show VESC values (voltage, current, temp, RPM, etc.) |
| `rt` | `rtdata` | Show Refloat real-time data (state, pitch, roll, speed, setpoints) |
| `all [mode]` | `alldata` | Show Refloat compact data (mode 0-4, default 2) |
| `watch [dur]` | `w` | Live-stream RT data (default 500ms interval, Enter to stop) |
| `alive` | | Send keepalive to VESC |

#### Simulator Control (only with `--sim`)

| Command | Aliases | Description |
|---------|---------|-------------|
| `footpad <state>` | `fp` | Set footpad: `none`, `left`, `right`, `both` |
| `pitch <degrees>` | | Set board pitch in degrees |
| `roll <degrees>` | | Set board roll in degrees |
| `state` | `st` | Show simulator internal state |

#### General

| Command | Aliases | Description |
|---------|---------|-------------|
| `help` | `h`, `?` | Show help |
| `quit` | `exit`, `q` | Exit |

---

## Transport Layer

### TCP

Standard TCP connection to VESC. The simulator listens on the address specified by `--sim-addr`. VESC packets are framed and sent/received over the TCP stream.

### BLE (Bluetooth Low Energy)

Emulates a VESC Express using the Nordic UART Service (NUS):

| UUID | Role |
|------|------|
| `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | Service UUID |
| `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | RX Characteristic (client writes here) |
| `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | TX Characteristic (server sends notifications) |

BLE packets are chunked at 20-byte MTU. No additional BLE-level framing — standard VESC packet framing is used within the BLE stream.

---

## VESC Packet Framing

All communication uses VESC's standard packet format:

**Short packet** (payload < 256 bytes):
```
[0x02] [len:1] [payload...] [crc16:2] [0x03]
```

**Long packet** (payload >= 256 bytes):
```
[0x03] [len:2] [payload...] [crc16:2] [0x03]
```

- CRC is CRC16-CCITT/XModem over the payload
- All multi-byte integers are big-endian
- Maximum payload size: 8192 bytes

### Float Encoding Conventions

| Notation | Encoding | Example |
|----------|----------|---------|
| `float16(scale)` | `int16(value * scale)` | `float16(10)` → 2 bytes |
| `float32(scale)` | `int32(value * scale)` | `float32(100)` → 4 bytes |
| `float32_auto` | IEEE 754 float32, big-endian | 4 bytes |

---

## VESC Commands

Each command is identified by the first byte of the payload (`CommPacketID`).

### Telemetry Commands

#### COMM_FW_VERSION (0) — Firmware Version

**Request**: `[0x00]`

**Response**:

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | Command ID (0x00) |
| 1 | 1 | FW major version |
| 2 | 1 | FW minor version |
| 3 | var | HW name (null-terminated string) |
| var | 12 | UUID |
| var | 1 | isPaired (0/1) |
| var | 1 | FW test version (0 = release) |
| var | 1 | HW type (0=VESC, 3=VESC Express) |
| var | 1 | Custom config count (1 for Refloat) |
| var | 1 | Has phase filters (0/1) |
| var | 1 | QML HW UI (0=none, 1=has, 2=fullscreen) |
| var | 1 | QML App UI (0=none, 1=has, 2=fullscreen) |
| var | 1 | NRF flags (bit0=nameSupported, bit1=pinSupported) |
| var | var | FW name (null-terminated string) |
| var | 4 | HW CRC (uint32) |

#### COMM_GET_VALUES (4) — Motor Telemetry

**Request**: `[0x04]`

**Response** (67 bytes):

| Field | Type | Scale |
|-------|------|-------|
| temp_fet | float16 | 10 |
| temp_motor | float16 | 10 |
| avg_motor_current | float32 | 100 |
| avg_input_current | float32 | 100 |
| avg_id | float32 | 100 |
| avg_iq | float32 | 100 |
| duty_cycle | float16 | 1000 |
| rpm | float32 | 1 |
| input_voltage | float16 | 10 |
| amp_hours | float32 | 10000 |
| amp_hours_charged | float32 | 10000 |
| watt_hours | float32 | 10000 |
| watt_hours_charged | float32 | 10000 |
| tachometer | int32 | — |
| tachometer_abs | int32 | — |
| fault_code | uint8 | — |
| pid_pos_now | float32 | 1000000 |
| controller_id | uint8 | — |
| temp_mos1 | float16 | 10 |
| temp_mos2 | float16 | 10 |
| temp_mos3 | float16 | 10 |
| avg_vd | float32 | 1000 |
| avg_vq | float32 | 1000 |
| status | uint8 | — |

#### COMM_GET_VALUES_SETUP (47) — Aggregated Setup Telemetry

**Request**: `[0x2F]`

**Response**: Aggregated view for multi-VESC setups including temperature, currents, duty, RPM, speed, voltage, battery level, amp/watt hours, distance, fault, controller ID, number of VESCs, odometer, and uptime.

#### COMM_GET_VALUES_SELECTIVE (50) / COMM_GET_VALUES_SETUP_SELECTIVE (51)

Same as COMM_GET_VALUES but with a bitmask to select which fields to include. The simulator returns full values for all selective requests.

#### COMM_GET_IMU_DATA (65) — IMU Sensor Data

**Request**: `[0x41] [mask_hi:1] [mask_lo:1]`

Bitmask controls which data groups are included:

| Bit | Data | Fields |
|-----|------|--------|
| 0 | Euler angles | roll, pitch, yaw (float32, scale=1e6) |
| 1 | Accelerometer | x, y, z (float32, scale=1e6) |
| 2 | Gyroscope | x, y, z (float32, scale=1e6) |
| 3 | Magnetometer | x, y, z (float32, scale=1e6) |
| 4 | Quaternion | w, x, y, z (float32, scale=1e6) |

#### COMM_GET_DECODED_ADC (32) — Footpad ADC Values

**Response**: decoded1, voltage1, decoded2, voltage2 (4x float32, scale=1e6)

#### COMM_GET_DECODED_PPM (31) — PPM Input

**Response**: decoded_value, pulse_length (2x float32, scale=1e6)

#### COMM_GET_DECODED_CHUK (33) — Nunchuk Input

**Response**: decoded_y (float32, scale=1e6)

#### COMM_GET_DECODED_BALANCE (79) — Balance Data

**Response**: pitch, roll, diff_time, motor_current (4x float32, scale=1e6)

#### COMM_GET_STATS (128) — Usage Statistics

**Request**: `[0x80] [mask_hi:1] [mask_lo:1]`

Bitmask-based statistics including speed totals, distance, current, charge, watt-hours, time, and temperature averages.

#### COMM_GET_BATTERY_CUT (115) — Battery Cutoff Voltages

**Response**: start_voltage, end_voltage (2x float32, scale=1000)

### Configuration Commands

#### COMM_GET_MCCONF (14) / COMM_GET_MCCONF_DEFAULT (15) — Motor Config

Returns the serialized motor controller configuration blob (~488 bytes for FW 6.5). The binary format is auto-generated by VESC's confgenerator from the motor config XML.

#### COMM_SET_MCCONF (13) — Write Motor Config

Accepts and stores the serialized motor config.

#### COMM_GET_APPCONF (17) / COMM_GET_APPCONF_DEFAULT (18) — App Config

Returns the serialized app configuration blob (~360 bytes). Contains controller ID, CAN settings, app selection, IMU config, etc.

#### COMM_SET_APPCONF (16) — Write App Config

Accepts and stores the serialized app config.

#### COMM_GET_CUSTOM_CONFIG_XML (92) — Refloat Config XML

Chunked download of the Refloat `settings.xml` (~350KB). VESC Tool uses this to render the configuration UI.

**Request**: `[0x5C] [confInd:1] [requestLen:4] [offset:4]`

**Response**: `[0x5C] [confInd:1] [totalSize:4] [offset:4] [data...]`

#### COMM_GET_CUSTOM_CONFIG (93) / COMM_GET_CUSTOM_CONFIG_DEFAULT (94)

Returns the serialized Refloat configuration binary. Generated from XML defaults using VESC confparser serialization rules:

| XML Type | Encoding |
|----------|----------|
| 0 (int) | int32 big-endian |
| 1 (double) | float32_auto (IEEE754 big-endian) |
| 2 (enum) | int32 big-endian |
| 3 (string) | skipped (not transmitted) |
| 4 (bool) | uint8 (0 or 1) |

Fields are serialized in XML document order. Fields with `<transmittable>0</transmittable>` are skipped.

#### COMM_SET_CUSTOM_CONFIG (95) — Write Refloat Config

Accepts and stores the serialized Refloat configuration.

#### COMM_SET_MCCONF_TEMP (48) / COMM_SET_MCCONF_TEMP_SETUP (49)

Temporary motor config changes (not persisted). Accepted silently.

#### COMM_SET_BATTERY_CUT (86)

Set battery cutoff voltages. Accepted silently.

### QML UI Commands

#### COMM_GET_QML_UI_HW (117) / COMM_GET_QML_UI_APP (118)

Chunked QML UI data download. The simulator returns empty (size=0) since no QML UI is provided.

### CAN Bus Commands

#### COMM_PING_CAN (62) — CAN Bus Ping

**Response**: `[0x3E] [controller_id]`

Returns the simulator's controller ID.

#### COMM_FORWARD_CAN (34) — CAN Forwarding

Forwards the inner command payload. The simulator handles it locally.

### System Commands

| Command | ID | Behavior |
|---------|----|----------|
| COMM_ALIVE (30) | 0x1E | Keepalive, no response |
| COMM_REBOOT (29) | 0x1D | Logged and ignored |
| COMM_SHUTDOWN (156) | 0x9C | Logged and ignored |
| COMM_APP_DISABLE_OUTPUT (63) | 0x3F | Accepted silently |
| COMM_SET_ODOMETER (110) | 0x6E | Accepted silently |
| COMM_RESET_STATS (129) | 0x81 | Accepted silently |
| COMM_BMS_GET_VALUES (96) | 0x60 | Returns minimal empty response |

### Motor Control Commands (Fire-and-Forget)

| Command | ID | Description |
|---------|----|-------------|
| COMM_SET_DUTY (5) | 0x05 | Set duty cycle |
| COMM_SET_CURRENT (6) | 0x06 | Set motor current |
| COMM_SET_CURRENT_BRAKE (7) | 0x07 | Set brake current |
| COMM_SET_RPM (8) | 0x08 | Set target RPM |
| COMM_SET_POS (9) | 0x09 | Set target position |
| COMM_SET_HANDBRAKE (10) | 0x0A | Set handbrake |
| COMM_SET_CURRENT_REL (84) | 0x54 | Set relative current |

### Terminal Commands

#### COMM_TERMINAL_CMD (20) / COMM_TERMINAL_CMD_SYNC (64)

**Request**: `[cmd_id] [text...]`

**Response** (via COMM_PRINT_TEXT): Echoes back with basic commands:
- `help` — shows available commands
- `faults` — shows fault log
- `status` — shows simulator state

### IMU Calibration

#### COMM_GET_IMU_CALIBRATION (90)

**Response**: 6x float32(scale=1000) — accelerometer and gyroscope offsets (all zeros in simulator).

---

## Refloat Commands

Refloat commands are wrapped in `COMM_CUSTOM_APP_DATA` (ID=36):

```
[0x24] [0x65] [command_id] [payload...]
```

- `0x24` = COMM_CUSTOM_APP_DATA
- `0x65` = Refloat magic byte (101)

### Query Commands (Request/Response)

#### COMMAND_INFO (0) — Package Info

**Response (v2 format)**:

| Offset | Size | Field |
|--------|------|-------|
| 0-2 | 3 | Header (0x24, 0x65, 0x00) |
| 3 | 1 | Response version (2) |
| 4 | 1 | Flags |
| 5 | 20 | Package name (null-padded) |
| 25 | 1 | Major version |
| 26 | 1 | Minor version |
| 27 | 1 | Patch version |
| 28 | 20 | Version suffix (null-padded) |
| 48 | 4 | Git hash (uint32) |
| 52 | 4 | Tick rate Hz (uint32) |
| 56 | 4 | Capabilities bitmask (uint32) |
| 60 | 1 | Extra flags |

#### COMMAND_GET_RTDATA (1) — Legacy Real-Time Data

Returns 72 bytes of telemetry using float32_auto encoding:

- Balance current, pitch, roll
- State byte: `(SAT_compat << 4) | state_compat`
- Switch byte: footpad state
- ADC1, ADC2
- Setpoint + 5 sub-setpoints (ATR, brake tilt, torque tilt, turn tilt, remote)
- Raw pitch, filtered current, accel diff, booster current, dir current, remote input

#### COMMAND_GET_ALLDATA (10) — Compact Real-Time Data

**Request**: `[0x65] [10] [mode]` (mode 1-4)

Returns compact data using uint8 and float16 encodings. Higher modes include more data:

| Mode | Additional Data |
|------|----------------|
| 1 | Core: current, pitch, roll, state, footpad, ADC, setpoints, voltage, ERPM, speed |
| 2 | + distance, MOSFET temp, motor temp |
| 3 | + odometer, amp hours, watt hours, battery level |
| 4 | + charging current and voltage |

On VESC fault, returns: `[0x65] [10] [69] [fault_code]`

#### COMMAND_REALTIME_DATA (31) — New-Style Real-Time Data

Modern telemetry format with conditional sections:

1. **Header**: mask byte, extra_flags, timestamp, state packing
2. **Always**: 16 float16 values (speed, ERPM, current, duty, voltage, temps, pitch, roll, ADC, remote)
3. **If running** (mask bit 0): 10 additional float16 values (setpoints, balance current, ATR data, booster)
4. **If charging** (mask bit 1): charging current and voltage
5. **Always** (mask bit 2): active_alert_mask (uint32), reserved (uint32), fault code (uint8)

#### COMMAND_REALTIME_DATA_IDS (32) — Field Name Strings

Returns the names of all RT data fields as length-prefixed strings. Used by VESC Tool to label graph axes.

16 always-present items:
`motor.speed`, `motor.erpm`, `motor.current`, `motor.dir_current`, `motor.filt_current`, `motor.duty_cycle`, `motor.batt_voltage`, `motor.batt_current`, `motor.mosfet_temp`, `motor.motor_temp`, `imu.pitch`, `imu.balance_pitch`, `imu.roll`, `footpad.adc1`, `footpad.adc2`, `remote.input`

10 runtime-only items:
`setpoint`, `atr.setpoint`, `brake_tilt.setpoint`, `torque_tilt.setpoint`, `turn_tilt.setpoint`, `remote.setpoint`, `balance_current`, `atr.accel_diff`, `atr.speed_boost`, `booster.current`

#### COMMAND_ALERTS_LIST (35) — Alert History

**Response**: active_alert_mask (uint32), reserved (uint32), fault code (uint8), alert count (uint8), followed by alert records.

#### COMMAND_LCM_POLL (24) — Light Module Poll

**Response**: State byte (state + footpad + handtest), fault code, duty/pitch, ERPM, current, voltage, brightness values.

#### COMMAND_LCM_LIGHT_INFO (25) — Lighting Info

**Response**: LED type byte (0 = no LCM, 3 = LCM enabled). If LCM enabled: brightness, mode, and count fields.

#### COMMAND_LCM_DEVICE_INFO (27) — LCM Hardware Info

**Response**: LCM firmware name (null-terminated string, empty if no LCM).

#### COMMAND_LCM_GET_BATTERY (29) — Battery Level

**Response**: Battery level as float32_auto (0.0-1.0).

#### COMMAND_LIGHTS_CONTROL (20) — Control Lights

**Response**: Status byte: `headlights_enabled << 1 | enabled`

### Fire-and-Forget Commands (No Response)

| Command | ID | Description |
|---------|----|-------------|
| COMMAND_RT_TUNE | 2 | Apply runtime PID tune (nibble-encoded) |
| COMMAND_TUNE_DEFAULTS | 3 | Reset tune to compiled defaults |
| COMMAND_CFG_SAVE | 4 | Save config to EEPROM |
| COMMAND_CFG_RESTORE | 5 | Restore config from EEPROM |
| COMMAND_TUNE_OTHER | 6 | Apply startup/tilt tune |
| COMMAND_RC_MOVE | 7 | Remote-control motor while idle |
| COMMAND_BOOSTER | 8 | Change booster settings |
| COMMAND_PRINT_INFO | 9 | No-op |
| COMMAND_EXPERIMENT | 11 | No-op |
| COMMAND_LOCK | 12 | Lock/disable board |
| COMMAND_HANDTEST | 13 | Toggle hand-test mode |
| COMMAND_TUNE_TILT | 14 | Apply tilt/duty tune |
| COMMAND_FLYWHEEL | 22 | Toggle flywheel mode |
| COMMAND_LCM_LIGHT_CTRL | 26 | Control LCM lighting |
| COMMAND_CHARGING_STATE | 28 | Update charging state (from LCM) |
| COMMAND_ALERTS_CONTROL | 36 | Clear alerts/fatal errors |
| COMMAND_DATA_RECORD_REQ | 41 | Data recorder control |

---

## Simulator Configuration

### Default Board State

The simulator starts with these defaults:

| Parameter | Default Value |
|-----------|---------------|
| FW Version | 6.5 |
| Hardware Name | VESC 6 MK6 |
| Controller ID | 0 |
| Battery Voltage | 63.0V (15S fully charged) |
| MOSFET Temperature | 28.0°C |
| Motor Temperature | 25.0°C |
| Run State | READY |
| Mode | NORMAL |
| Package Name | Refloat |
| Package Version | 2.0.1-sim |

### Physics Simulation

The simulator runs a 50Hz physics loop that models:

- **State machine**: READY → RUNNING when both footpads engaged and pitch < 5° and roll < 30°
- **Fault detection**: Pitch > 15° (pitch fault), roll > 60° (roll fault), footpad disengage (switch fault)
- **Balance physics**: Pitch drives motor current via P controller (`current = pitch * 2.0`)
- **Speed model**: ERPM ≈ speed × 700, duty ≈ speed / 15.0
- **Thermal model**: Temperatures rise slowly under load
- **Energy counters**: Amp-hours and watt-hours accumulate during riding
- **Sensor noise**: Small random noise on ADC values

### Refloat Config XML

The full Refloat `settings.xml` (~350KB, ~4355 lines) is embedded in the binary. It contains all Refloat configuration parameters with types, defaults, min/max values, descriptions, and VESC Tool UI layout information. VESC Tool downloads this via `COMM_GET_CUSTOM_CONFIG_XML` using chunked transfers.

### Fault Codes

| Code | Name |
|------|------|
| 0 | NONE |
| 1 | OVER_VOLTAGE |
| 2 | UNDER_VOLTAGE |
| 3 | DRV |
| 4 | ABS_OVER_CURRENT |
| 5 | OVER_TEMP_FET |
| 6 | OVER_TEMP_MOTOR |
| 7 | GATE_DRIVER_OVER_VOLTAGE |
| 8 | GATE_DRIVER_UNDER_VOLTAGE |
| 9 | MCU_UNDER_VOLTAGE |
| 10 | BOOTING_FROM_WATCHDOG |
| 11 | ENCODER_SPI |

### Run States

| State | Value | Description |
|-------|-------|-------------|
| DISABLED | 0 | Board disabled |
| STARTUP | 1 | Initializing |
| READY | 2 | Idle, waiting for rider |
| RUNNING | 3 | Actively balancing |

### Footpad States

| State | Value |
|-------|-------|
| NONE | 0 |
| LEFT | 1 |
| RIGHT | 2 |
| BOTH | 3 |
