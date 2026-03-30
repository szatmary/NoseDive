# NoseDive — New Board Setup Wizard

## Overview

When a user connects to a new board for the first time, the app walks them through a complete setup flow. The wizard ensures firmware is current (in the correct update order), the motor is characterized, sensors are calibrated, and ride parameters are configured.

## Wizard Flow

```
┌─────────────┐
│  BLE Scan &  │
│  Connect     │
└──────┬──────┘
       ▼
┌─────────────┐
│  Identify    │
│  Hardware    │
└──────┬──────┘
       ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ 1. Firmware  │────►│ 2. Firmware  │────►│ 3. Firmware  │
│ VESC Express │     │ VESC Motor   │     │ BMS          │
│ (ESP32/BLE)  │     │ Controller   │     │ (if present) │
└──────┬──────┘     └──────┬──────┘     └──────┬──────┘
       │                   │                   │
       └───────────┬───────┘───────────────────┘
                   ▼
          ┌─────────────┐
          │ 4. Motor     │
          │ Detection    │
          └──────┬──────┘
                 ▼
          ┌─────────────┐
          │ 5. IMU       │
          │ Calibration  │
          └──────┬──────┘
                 ▼
          ┌─────────────┐
          │ 6. Board     │
          │ Profile      │
          └──────┬──────┘
                 ▼
          ┌─────────────┐
          │ 7. Ride      │
          │ Config       │
          └──────┬──────┘
                 ▼
          ┌─────────────┐
          │    Done!     │
          └─────────────┘
```

---

## Step 0: BLE Scan & Connect

- Scan for BLE peripherals advertising the Nordic UART Service UUID (`6e400001-b5a3-f393-e0a9-e50e24dcca9e`)
- Display discovered boards by name (BLE local name, e.g. "Refloat XXXX")
- User taps to connect
- After connection, send `COMM_FW_VERSION` (0) to identify the hardware

### Hardware Identification

The `COMM_FW_VERSION` response tells us:
- **FW major/minor** — current firmware version
- **HW type** — `0` = VESC, `3` = VESC Express
- **HW name** — controller hardware string (e.g. "VESC 6 MK6", "Thor301")
- **Custom config count** — `1` if Refloat is installed
- **FW name** — package name (e.g. "Refloat")
- **UUID** — 12-byte unique controller ID (used to remember boards)

If the initial BLE connection is to the VESC Express (HW type 3), it acts as the BLE-to-CAN bridge. The main VESC motor controller sits behind it on the CAN bus and is reached via `COMM_FORWARD_CAN` (34).

---

## Step 1: Firmware — VESC Express (ESP32)

**Why first?** The VESC Express is the BLE radio. If its firmware is too old, it may not support the commands needed to update the downstream VESC or BMS. Updating it first ensures reliable communication for the remaining steps.

### Flow

1. Query current VESC Express firmware via `COMM_FW_VERSION`
2. Compare against latest known release
3. If current → show green checkmark, skip to Step 2
4. If outdated → show changelog, prompt user to update
5. Update sequence:
   - `COMM_JUMP_TO_BOOTLOADER` (1) — enter bootloader mode
   - `COMM_ERASE_NEW_APP` (2) — erase application flash
   - `COMM_WRITE_NEW_APP_DATA` (3) — write firmware in chunks (progress bar)
   - Wait for reboot, reconnect BLE
   - Re-query `COMM_FW_VERSION` to confirm

### UI

- Card showing: current version, latest version, HW name
- "Update Firmware" button (or "Up to Date" badge)
- Progress bar during flash write
- Warning: "Do not disconnect power during update"
- After update: "Reconnecting..." spinner, then confirmation

### Error Handling

- Flash write failure → retry from erase step
- BLE disconnect during update → prompt user to power cycle and reconnect
- Version mismatch after update → flag as error, offer retry

---

## Step 2: Firmware — VESC Motor Controller

**Why second?** The main VESC runs the FOC motor control and Refloat package. It must be updated after the Express (which provides the communication link) but before motor detection (which depends on the FOC engine).

### Flow

1. Query VESC FW version (via CAN forward if connected through Express)
2. Compare against latest compatible release
3. Check Refloat package version separately via `COMM_CUSTOM_APP_DATA` → `COMMAND_INFO` (0)
4. Update VESC firmware if needed (same bootloader sequence as Step 1)
5. Update Refloat package if needed (same mechanism — Refloat is flashed as the custom app)

### UI

- Two version rows: "VESC Firmware" and "Refloat Package"
- Each shows current vs latest
- Update buttons are separate (user may want one but not the other)
- Same progress bar / warning pattern as Step 1

### Important Notes

- The VESC firmware and Refloat package are separate binaries but share the flash
- Updating VESC firmware may wipe the Refloat package (depends on version)
- Always update VESC firmware first, then re-flash Refloat if needed
- After update, re-query both versions to confirm

---

## Step 3: Firmware — BMS (if present)

**Why third?** The BMS communicates via CAN bus through the VESC. Both upstream controllers must be current before touching the BMS.

### Flow

1. Query BMS presence via `COMM_BMS_GET_VALUES` (96)
2. If no BMS → skip this step entirely
3. If present, query BMS firmware version
4. Compare against latest, update if needed
5. BMS update uses the same VESC bootloader protocol forwarded over CAN

### UI

- "No BMS detected — skipping" (auto-advance after 1s)
- Or: BMS version card with update option
- Same progress / warning pattern

---

## Step 4: Motor Detection

**Why here?** All firmware is now current, so the FOC engine has the latest detection algorithms. Motor parameters are needed before configuring ride behavior.

### What it does

Motor detection spins the wheel briefly to measure electrical characteristics. The board must be elevated (wheel free to spin). This replaces guessing at motor parameters.

### Flow

1. Prompt user: "Elevate the board so the wheel can spin freely"
2. User confirms wheel is free
3. Run detection sequence:
   - `COMM_DETECT_MOTOR_R_L` (25) — measure resistance and inductance
     - Response: R (×1e6), L (×1e3), Ld-Lq diff (×1e3)
     - Typical hub motor: R ≈ 88mΩ, L ≈ 233µH
   - `COMM_DETECT_MOTOR_FLUX_OPENLOOP` (57) — open-loop spin to measure flux linkage
     - Response: flux (×1e7), encoder offset (×1e6), encoder ratio (×1e6), inverted
     - Typical hub motor: flux ≈ 28mWb
   - `COMM_DETECT_HALL_FOC` (28) — slow spin to map hall sensor positions
     - Response: 8-byte hall table + result code
     - Typical: [255, 1, 3, 2, 5, 6, 4, 255]
4. Display results to user
5. `COMM_DETECT_APPLY_ALL_FOC` (58) — write detected values into MC_CONF
   - Response: result code (0 = success)

### UI

- Illustration showing board elevated on a stand
- "Start Detection" button
- Animated wheel-spin indicator during each phase
- Results summary:
  - Resistance: 88 mΩ ✓
  - Inductance: 233 µH ✓
  - Flux linkage: 28.0 mWb ✓
  - Hall sensors: OK ✓
- "Apply to Controller" button
- Note: L may show ≈0 for hub motors at the 1e3 scale — this is expected

### Error Handling

- Wheel not spinning (R/L values unreasonable) → "Is the wheel free to spin?"
- Hall sensor detection failure (result ≠ 0) → "Check hall sensor wiring"
- Flux detection timeout → retry with increased current

---

## Step 5: IMU Calibration

**Why here?** Motor is characterized, now calibrate the balance sensor. The IMU (typically MPU6050 or ICM-20948) needs a level reference for pitch/roll zeroing.

### Flow

1. Prompt user: "Place the board on a flat, level surface"
2. User confirms board is level and stationary
3. Send `COMM_GET_IMU_CALIBRATION` (90) to read current calibration
4. Initiate IMU calibration (gyro offset + accelerometer level reference)
5. Show live IMU data during calibration:
   - Pitch, roll values settling toward zero
   - Gyro bias values converging
6. Calibration completes when values stabilize (typically 2-5 seconds)
7. Save calibration to controller

### UI

- Illustration showing board flat on ground
- Live pitch/roll gauge (spirit level style)
- "Calibrate" button
- Progress indicator: "Sampling... (3s)"
- Result: "Pitch offset: 0.3° — Roll offset: -0.1° ✓"
- "Save Calibration" button

### What's Calibrated

- **Gyro bias** — zero-rate offset for pitch/roll/yaw gyroscope channels
- **Accelerometer reference** — defines "level" for the balance controller
- **Pitch offset** — compensates for sensor mounting angle vs board plane

### Error Handling

- Board not level (accelerometer reads > ±5°) → "Board doesn't appear level"
- IMU not responding → "IMU communication error — check hardware"
- Unstable readings (vibration) → "Place on a stable surface, avoid vibration"

---

## Step 6: Board Profile

**Why here?** Hardware is fully characterized. Now set the metadata that the app uses for speed calculation, battery SOC, and ride telemetry display.

### Flow

1. If a known board profile matches (by UUID or HW name) → pre-fill
2. Otherwise, present a form:

| Field | Source | Example |
|-------|--------|---------|
| Board name | User input | "My Funwheel X7" |
| Controller | Auto-detected from FW_VERSION | Thor301 |
| Motor | From detection or manual | Superflux HS Mk3 |
| Pole pairs | From detection | 15 |
| Battery config | User selects or manual | 20s2p Samsung 50S |
| Wheel diameter | User input | 11.0" |
| Tire model | User input (optional) | Fungineers Thunder |

3. Save as JSON profile (on-device + optional cloud sync)

### UI

- Pre-filled card with auto-detected values highlighted
- Editable fields for things we can't detect (battery config, wheel size)
- Dropdown for common battery/motor combos
- "Save Profile" button

### Computed Values (shown after save)

- Top ERPM → speed conversion factor
- Battery voltage range → SOC curve
- Expected range estimate

---

## Step 7: Ride Configuration

**Why last?** All hardware parameters are known. Now configure the riding behavior — Refloat tune, safety limits, and personal preferences.

### Flow

1. Load current Refloat config via `COMM_GET_CUSTOM_CONFIG` (93)
2. Present grouped settings:

**Safety**
- Tiltback speed
- Tiltback duty
- Tiltback high/low voltage
- Nosedive prevention aggressiveness

**Ride Feel**
- ATR (Adaptive Torque Response) strength
- Turn tilt compensation
- Speed boost factor
- Brake curve

**Startup**
- Footpad sensitivity
- Startup pitch tolerance
- Remote input (if applicable)

3. Offer presets: "Beginner", "Intermediate", "Aggressive"
4. Save via `COMM_SET_CUSTOM_CONFIG` (95) + `COMMAND_CFG_SAVE` (4)

### UI

- Grouped cards with sliders and toggles
- Preset buttons at top
- "Advanced" toggle to show all parameters
- Live preview values where possible (e.g., "Tiltback at 35 mph")
- "Save to Board" button
- Note: Changes take effect immediately but aren't persistent until saved

---

## Wizard State Machine

The wizard tracks progress so users can resume if interrupted:

```
STATES:
  not_started
  connecting
  fw_express          (Step 1)
  fw_express_updating
  fw_vesc             (Step 2)
  fw_vesc_updating
  fw_bms              (Step 3)
  fw_bms_updating
  motor_detection     (Step 4)
  motor_detecting
  imu_calibration     (Step 5)
  imu_calibrating
  board_profile       (Step 6)
  ride_config         (Step 7)
  complete
```

State is persisted per board UUID. If the user disconnects mid-wizard:
- On reconnect, resume from the last incomplete step
- Completed steps show checkmarks and can be re-run
- User can skip non-critical steps (profile, ride config) and come back later

---

## Skip / Re-run Policy

| Step | Skippable? | Re-runnable? | Notes |
|------|-----------|-------------|-------|
| FW Express | Yes (if current) | Yes | Auto-skips if up to date |
| FW VESC | Yes (if current) | Yes | Auto-skips if up to date |
| FW BMS | Yes (if absent) | Yes | Auto-skips if no BMS |
| Motor Detection | No (first time) | Yes | Required for safe operation |
| IMU Calibration | No (first time) | Yes | Required for balance |
| Board Profile | Yes (defaults work) | Yes | Recommended but not blocking |
| Ride Config | Yes (defaults work) | Yes | Can tune later from main app |

---

## Data Flow Summary

```
VESC Express ──BLE──► App
     │
     │ CAN bus
     ▼
VESC Motor Controller ◄──► App (via CAN forward)
     │
     │ CAN bus
     ▼
BMS (optional) ◄──► App (via CAN forward)
```

All communication flows through the VESC Express BLE connection. Commands to the motor controller and BMS are wrapped in `COMM_FORWARD_CAN` (34) with the target CAN ID.
