# NoseDive — New Board Setup Wizard

## Overview

When a user connects to a new board for the first time, the app walks them through a complete setup flow. The wizard ensures firmware is current (in the correct update order), the motor is characterized, sensors are calibrated, and ride parameters are configured.

The wizard also handles configuration backup/restore, wheel calibration, peripheral setup (LEDs, remotes), and multi-board management.

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
┌─────────────┐
│ 1. Backup    │
│ Config       │
└──────┬──────┘
       ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ 2. Firmware  │────►│ 3. Firmware  │────►│ 4. Firmware  │
│ VESC Express │     │ VESC Motor   │     │ BMS          │
│ (ESP32/BLE)  │     │ Controller   │     │ (if present) │
└──────┬──────┘     └──────┬──────┘     └──────┬──────┘
       │                   │                   │
       └───────────┬───────┘───────────────────┘
                   ▼
          ┌─────────────┐
          │ 5. Motor     │
          │ Detection    │
          └──────┬──────┘
                 ▼
          ┌─────────────┐
          │ 6. IMU       │
          │ Calibration  │
          └──────┬──────┘
                 ▼
          ┌─────────────┐
          │ 7. Battery   │
          │ Cutoffs      │
          └──────┬──────┘
                 ▼
          ┌─────────────┐
          │ 8. Wheel     │
          │ Calibration  │
          └──────┬──────┘
                 ▼
          ┌─────────────┐
          │ 9. Board     │
          │ Profile      │
          └──────┬──────┘
                 ▼
          ┌─────────────┐
          │ 10. Ride     │
          │ Config       │
          └──────┬──────┘
                 ▼
          ┌─────────────┐
          │ 11. LEDs &   │
          │ Peripherals  │
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

## Step 1: Configuration Backup

**Why first?** Before touching any firmware, snapshot the current state. If a firmware update goes wrong or changes defaults, we can restore the user's working configuration.

### Flow

1. Read and store the following from the board:
   - `COMM_GET_MC_CONF` (14) — full motor controller configuration
   - `COMM_GET_APP_CONF` (17) — full app configuration
   - `COMM_GET_CUSTOM_CONFIG` (93) — Refloat configuration (if installed)
   - `COMM_GET_CUSTOM_CONFIG_XML` (92) — Refloat config XML schema
   - `COMM_FW_VERSION` (0) — firmware version metadata
   - Board UUID from FW_VERSION response
2. Bundle into a timestamped backup file on-device
3. Optionally upload to cloud (user's account)

### Backup File Format

```json
{
  "version": 1,
  "timestamp": "2026-03-30T12:00:00Z",
  "board_uuid": "A1B2C3D4E5F6...",
  "board_name": "My Funwheel X7",
  "firmware": {
    "vesc_major": 6,
    "vesc_minor": 5,
    "refloat_version": "2.0.1-sim",
    "hw_name": "Thor301"
  },
  "mc_conf": "<base64-encoded binary>",
  "app_conf": "<base64-encoded binary>",
  "custom_conf": "<base64-encoded binary>",
  "custom_conf_xml": "<base64-encoded compressed XML>"
}
```

### UI

- "Backing up current configuration..." with progress indicator
- Shows what's being backed up (MC_CONF, APP_CONF, Refloat)
- On first-time setup (blank board): "No existing config — skipping backup"
- "Backup Complete" with file size and location

### Restore Flow (accessible from settings, not wizard)

1. User selects a backup file (local or cloud)
2. Validate: check board UUID matches (warn if different board)
3. Validate: check firmware version compatibility
4. Write configs back:
   - `COMM_SET_MC_CONF` (13)
   - `COMM_SET_APP_CONF` (16)
   - `COMM_SET_CUSTOM_CONFIG` (95) + `COMMAND_CFG_SAVE` (4)
5. Reboot controller via `COMM_REBOOT` (29)

### Error Handling

- Read failure on any config → still backup what we got, flag incomplete
- Cloud upload failure → keep local copy, retry later
- Restore to different board → warn but allow (configs may be transferable)

---

## Step 2: Firmware — VESC Express (ESP32)

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

## Step 3: Firmware — VESC Motor Controller + Refloat

**Why second?** The main VESC runs the FOC motor control and Refloat package. It must be updated after the Express (which provides the communication link) but before motor detection (which depends on the FOC engine).

### Flow

1. Query VESC FW version (via CAN forward if connected through Express)
2. Compare against latest compatible release
3. **Detect Refloat**: check `custom_config_count` from FW_VERSION response
   - If `custom_config_count == 0` → Refloat is **not installed**
   - If `custom_config_count >= 1` → query version via `COMM_CUSTOM_APP_DATA` → `COMMAND_INFO` (0)
4. Update VESC firmware if needed (same bootloader sequence as Step 2)
5. Install or update Refloat:

### Refloat Detection & Installation

#### Case A: Refloat Not Installed

This is common on brand-new VESCs or after a VESC firmware update that wiped the custom app partition.

1. Show: "Refloat is not installed. This package provides balance control for your onewheel."
2. Prompt: "Install Refloat?" with version info and changelog
3. Flash Refloat binary to the custom app partition:
   - `COMM_ERASE_NEW_APP` (2) — erase custom app area
   - `COMM_WRITE_NEW_APP_DATA` (3) — write Refloat binary in chunks
   - Wait for reboot
4. Re-query `COMM_FW_VERSION` to confirm `custom_config_count == 1`
5. Verify via `COMMAND_INFO` (0) that Refloat responds with correct version

#### Case B: Refloat Installed but Outdated

1. Show current vs latest version
2. Prompt: "Update Refloat from 1.2.0 to 2.0.1?"
3. Same flash sequence as Case A
4. Note: Refloat config will be reset to defaults — this is why Step 1 (backup) matters

#### Case C: Refloat Up to Date

1. Show green checkmark: "Refloat 2.0.1 ✓"
2. Auto-advance

#### Case D: Wrong Package Installed

If `custom_config_count >= 1` but `COMMAND_INFO` returns a different package name (e.g., "Balance" instead of "Refloat"):

1. Warn: "A different balance package ([name]) is installed"
2. Offer: "Replace with Refloat?" (destructive — explain consequences)
3. Or: "Keep current package" (wizard will adapt settings UI)

### UI

- Three rows showing status:
  - VESC Firmware: 6.05 ✓ (or "Update Available")
  - Refloat Package: Not Installed ⚠️ / 2.0.1 ✓ / Update Available
  - Hardware: Thor301
- Separate action buttons for VESC FW and Refloat
- Progress bar during flash
- Post-install verification spinner

### Important Notes

- The VESC firmware and Refloat package are separate binaries but share the flash
- Updating VESC firmware **will wipe** the Refloat package — always re-flash Refloat after a VESC FW update
- Always update VESC firmware first, then install/re-flash Refloat
- After any update, re-query both versions to confirm
- Refloat installation resets all custom config to defaults
- The backup from Step 1 can restore the previous Refloat config after re-installation

---

## Step 4: Firmware — BMS (if present)

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

## Step 5: Motor Detection

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

## Step 6: IMU Calibration

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

## Step 7: Battery Cutoffs

**Why here?** Motor and IMU are configured. Before the user rides, set safe voltage cutoffs to protect the battery pack. The profile step comes later, but cutoffs are safety-critical and should be written to the controller immediately.

### Flow

1. Read current battery cutoff settings via `COMM_GET_BATTERY_CUT` (115)
2. Auto-detect battery configuration if possible:
   - Measure current voltage via `COMM_GET_VALUES` (4)
   - Estimate series cell count: `round(voltage / 3.7)` (nominal Li-ion)
   - Suggest cutoffs based on cell count and chemistry
3. Present configuration:

| Parameter | How Set | Example (20s Li-ion) |
|-----------|---------|---------------------|
| Series cells | Auto-detected or user input | 20 |
| Chemistry | User selects | Li-ion (NMC) |
| Cell max voltage | From chemistry | 4.20V |
| Cell min voltage | From chemistry | 3.0V |
| Cutoff start | Calculated | 64.0V (3.2V/cell) |
| Cutoff end | Calculated | 60.0V (3.0V/cell) |
| Pack max voltage | Calculated | 84.0V |

4. Write to controller via `COMM_SET_BATTERY_CUT` (86)

### Common Cell Chemistry Profiles

| Chemistry | Nominal | Max | Cutoff Start | Cutoff End |
|-----------|---------|-----|-------------|------------|
| Li-ion NMC (Samsung 40T, 50S, Molicel P42A) | 3.6V | 4.20V | 3.2V | 3.0V |
| Li-ion NCA (Samsung 50E, 50G) | 3.6V | 4.20V | 3.1V | 2.8V |
| LiFePO4 | 3.2V | 3.65V | 2.9V | 2.5V |

### UI

- Auto-detected cell count shown prominently: "Detected: 20S pack (74.1V)"
- Chemistry dropdown with common cells pre-loaded
- Voltage bar visualization showing current voltage within the safe range
- Calculated cutoff values update live as user changes inputs
- "Apply Cutoffs" button
- Warning if cutoffs seem wrong: "Cutoff end (60V) is very low for a 20S pack"

### Error Handling

- Voltage doesn't match any reasonable cell count → ask user to input manually
- Cutoff end > cutoff start → validation error
- Cutoffs outside safe range for chemistry → warning (allow override for advanced users)

---

## Step 8: Wheel Calibration

**Why here?** Motor detection gives us ERPM. To convert ERPM to real speed and distance, we need accurate wheel circumference. The profile will store this, but we calibrate it here with an optional GPS-assisted method.

### Flow

#### Method A: Manual Entry (quick)

1. User selects tire size: 11.0", 11.5", or custom
2. Compute circumference: `π × diameter_inches × 0.0254`
3. Apply a tire pressure correction factor (optional)

#### Method B: GPS-Assisted Calibration (accurate)

1. User rides a known distance (e.g., between two landmarks, or a GPS-measured segment)
2. App records:
   - Tachometer start/end from `COMM_GET_VALUES` (tachometer_abs field)
   - GPS distance (if available) or user-entered distance
3. Compute actual circumference:
   ```
   tach_counts = tachometer_end - tachometer_start
   wheel_revolutions = tach_counts / (3 × motor_pole_pairs)
   circumference = distance / wheel_revolutions
   ```
4. Compare against nominal and show correction factor

#### Method C: Measure & Roll (no GPS needed)

1. Mark tire and ground at contact point
2. Push board forward exactly 10 wheel revolutions
3. Measure total distance rolled
4. `circumference = total_distance / 10`

### UI

- Three method tabs: "Quick Setup" / "GPS Calibration" / "Manual Measure"
- Quick: tire size selector with common sizes, pressure input
- GPS: "Start Ride" button → live distance counter → "Stop" → result
- Manual: step-by-step illustrated guide
- Result comparison: "Nominal: 878mm → Calibrated: 864mm (1.6% smaller)"
- Store calibrated value in profile

### Tire Pressure Effect

Tire pressure changes the effective diameter:
- Higher pressure → larger effective diameter (less tire deformation)
- Lower pressure → smaller effective diameter (more squish)
- A 5 PSI change can shift speed readings by ~2-3%
- The app should note the calibration pressure and warn if conditions change significantly

### Error Handling

- GPS accuracy too low (< 5m precision over short distance) → warn, suggest longer ride
- Zero tachometer change → "Wheel didn't spin — is the board on?"
- Unreasonable circumference (< 500mm or > 1200mm) → "Value seems wrong, re-check"

---

## Step 9: Board Profile

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

## Step 10: Ride Configuration

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

## Step 11: LEDs & Peripherals

**Why last?** All core functionality is configured. LEDs and peripherals are cosmetic / convenience features that don't affect ridability.

### LED / Light Control Module (LCM)

Refloat supports the LCM — an LED controller that integrates with board state (headlight, tail light, status, underglow).

#### Flow

1. Poll for LCM presence via `COMMAND_LCM_POLL` (24)
2. If present, query device info via `COMMAND_LCM_DEVICE_INFO` (27)
3. Query current light configuration via `COMMAND_LCM_LIGHT_INFO` (25)
4. Present configuration:

| Setting | Description |
|---------|-------------|
| Headlight mode | Always on / Auto (based on direction) / Off |
| Headlight brightness | 0-100% |
| Tail light mode | Always on / Brake flash / Off |
| Status LED pattern | Ride state / Battery SOC / Custom color |
| Underglow | Color / Pattern / Off |
| Brightness on idle | Dim to save power when stopped |

5. Write via `COMMAND_LCM_LIGHT_CTRL` (26)

#### UI

- Color pickers for customizable LEDs
- Mode dropdowns with preview descriptions
- "Preview" button — flashes the selected pattern on the board
- Save button

### Remote Pairing

If the user has a handheld remote (PPM, NRF, or UART-based):

#### Flow

1. Check APP_CONF for remote input type (PPM / NRF / UART / None)
2. If NRF remote: initiate pairing via `COMM_NRF_START_PAIRING` (37)
3. For PPM: guide user to calibrate throttle range
4. For UART remote: set baud rate and protocol in APP_CONF

#### UI

- Remote type selector
- Pairing mode button for NRF (with timeout countdown)
- PPM calibration: "Push throttle to max... now to min... center..."
- Live remote input display showing current value
- "Test Remote" button

### Error Handling

- LCM not detected → skip LED section, show "No LCM found"
- LCM firmware outdated → offer update (same bootloader flow)
- Remote pairing timeout → retry button
- PPM signal noisy → warn about interference

---

## Wizard State Machine

The wizard tracks progress so users can resume if interrupted:

```
STATES:
  not_started
  connecting
  backup              (Step 1)
  fw_express          (Step 2)
  fw_express_updating
  fw_vesc             (Step 3)
  fw_vesc_updating
  fw_refloat_install  (Step 3b — Refloat detection/install)
  fw_bms              (Step 4)
  fw_bms_updating
  motor_detection     (Step 5)
  motor_detecting
  imu_calibration     (Step 6)
  imu_calibrating
  battery_cutoffs     (Step 7)
  wheel_calibration   (Step 8)
  board_profile       (Step 9)
  ride_config         (Step 10)
  peripherals         (Step 11)
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
| Config Backup | Yes (first-time board) | Yes | Auto-skips if board is blank |
| FW Express | Yes (if current) | Yes | Auto-skips if up to date |
| FW VESC + Refloat | Yes (if current) | Yes | Auto-skips if up to date; installs Refloat if missing |
| FW BMS | Yes (if absent) | Yes | Auto-skips if no BMS |
| Motor Detection | No (first time) | Yes | Required for safe operation |
| IMU Calibration | No (first time) | Yes | Required for balance |
| Battery Cutoffs | No (first time) | Yes | Required for battery safety |
| Wheel Calibration | Yes (defaults work) | Yes | Manual entry OK, GPS calibration optional |
| Board Profile | Yes (defaults work) | Yes | Recommended but not blocking |
| Ride Config | Yes (defaults work) | Yes | Can tune later from main app |
| LEDs & Peripherals | Yes | Yes | Cosmetic, configure anytime |

---

## Firmware Update Sources

The app needs firmware binaries for VESC, VESC Express, Refloat, and BMS. Multiple source strategies:

### Bundled Firmware

- Ship known-good firmware versions inside the app binary
- Pros: works offline, guaranteed compatible, no server dependency
- Cons: requires app update to ship new firmware, increases app size
- Strategy: bundle the latest stable release at app build time

### GitHub Releases

- Pull from official repos at runtime:
  - VESC firmware: `vedderb/bldc` releases
  - VESC Express: `vedderb/vesc_express` releases
  - Refloat: `vedderb/refloat` releases (or community fork)
- Query GitHub API: `GET /repos/{owner}/{repo}/releases/latest`
- Download the `.bin` asset matching the hardware name
- Cache downloaded firmware on-device

### Hybrid (Recommended)

1. Ship bundled firmware as fallback
2. On app launch (with network), check GitHub for newer releases
3. If newer available, download in background and cache
4. Wizard uses cached version if newer than bundled, else bundled
5. Show "New firmware available" badge on main screen

### Firmware Manifest

```json
{
  "vesc_express": {
    "version": "6.05",
    "url": "https://github.com/vedderb/vesc_express/releases/...",
    "sha256": "abc123...",
    "min_hw": "VESC Express",
    "changelog": "Fixed BLE stability..."
  },
  "vesc_fw": {
    "version": "6.05",
    "url": "...",
    "sha256": "...",
    "variants": {
      "VESC 6 MK6": "bldc_6_mk6.bin",
      "Thor301": "bldc_thor301.bin"
    }
  },
  "refloat": {
    "version": "2.0.1",
    "url": "...",
    "sha256": "..."
  }
}
```

### Security

- Verify SHA256 hash of downloaded firmware before flashing
- Pin GitHub API certificate (prevent MITM)
- Never flash firmware from untrusted sources
- Keep audit log of firmware versions flashed per board

---

## Multi-Board Management

Users with multiple boards need to switch between them without re-running the wizard.

### Board Registry

Each connected board is identified by its 12-byte UUID from `COMM_FW_VERSION`. The app maintains a local registry:

```json
{
  "boards": [
    {
      "uuid": "A1B2C3D4E5F6A1B2C3D4E5F6",
      "name": "Funwheel X7 LR",
      "ble_name": "Refloat 4E5F",
      "ble_address": "AA:BB:CC:DD:EE:FF",
      "last_connected": "2026-03-30T12:00:00Z",
      "profile_path": "profiles/funwheel_x7_lr_hs.json",
      "wizard_state": "complete",
      "backups": ["backup_20260330_120000.json"]
    },
    {
      "uuid": "...",
      "name": "DIY Trail Board",
      "...": "..."
    }
  ],
  "active_board": "A1B2C3D4E5F6A1B2C3D4E5F6"
}
```

### Connection Flow

1. User opens app → BLE scan
2. If a known board is found → auto-connect (or show "Connect to [name]?")
3. If multiple known boards found → show picker
4. If unknown board found → start wizard
5. On connect, load that board's profile, config, and telemetry layout

### Board Switcher UI

- Main screen shows active board name and battery status
- Swipe or tap to switch between boards
- Each board has its own:
  - Profile (motor, battery, wheel)
  - Saved Refloat tune
  - Ride history / trip logs
  - Backup history
  - Custom dashboard layout

### Cloud Sync (Optional)

- Profiles and configs can sync across devices via user account
- Useful when switching between iPhone and iPad
- Backups stored in cloud as disaster recovery
- Never sync firmware binaries (too large, download fresh)

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
