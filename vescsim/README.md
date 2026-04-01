# VESC Simulator

A BLE/TCP VESC simulator that emulates a real board running Refloat. Connect with VESC Tool (iOS/desktop) over Bluetooth to configure, tune, and test.

## Build

```bash
go build -o vescsim ./vescsim/
```

## Quick Start

```bash
# Fresh unconfigured board (for setup wizard testing)
./vescsim -ble -web -fresh

# Resume from last session
./vescsim -ble -web

# Load a board profile
./vescsim -ble -web -profile profiles/funwheel_x7_lr_hs.json
```

## Flags

| Flag | Default | Description |
|------|---------|-------------|
| `-ble` | off | Enable BLE advertising (macOS only) |
| `-ble-name` | `VESC SIM` | BLE device name |
| `-web` | off | Enable web GUI |
| `-web-addr` | `127.0.0.1:8080` | Web GUI listen address |
| `-addr` | `127.0.0.1:0` | TCP listen address (random port) |
| `-data` | `vescsim_state.json` | State persistence file |
| `-profile` | none | Board profile JSON to load |
| `-fresh` | off | Start as unconfigured board |

## Fresh Board Mode (`-fresh`)

Simulates a brand new VESC with no configuration. Use this to test the VESC Tool setup wizard end-to-end.

What it does:
- Generates a random UUID (so VESC Tool sees it as a new board)
- Uses factory-default MCConf and AppConf
- Board starts idle (not riding)
- Deletes any saved state file

### Setup Wizard Walkthrough

1. Start the simulator:
   ```bash
   ./vescsim -ble -web -fresh
   ```

2. Open VESC Tool on your phone

3. Connect to "VESC SIM" via Bluetooth

4. Go through the setup wizard:
   - **Motor Type**: Large Inrunner (closest to onewheel hub motor)
   - **Direct Drive**: Check it
   - **Wheel Diameter**: 280mm (11" tire)
   - **Motor Poles**: 30 (15 pole pairs)
   - **Battery Cells**: number of series cells in your pack
   - **Temp Sensor**: KTY84, beta 4300
   - **Detect CAN**: Uncheck (single controller, no CAN bus)

5. Run motor detection — the simulator returns simulated R/L/flux values

6. Configure sensors, battery limits, etc.

All configuration writes are saved to `vescsim_state.json`. Next time you start without `-fresh`, your settings persist.

## Normal Mode (default)

Loads saved state from the previous session. The simulator remembers:
- Motor configuration (MCConf)
- App configuration (AppConf)
- Refloat custom config

The board starts in riding mode with a simulated cruise (pitch/roll oscillation, speed ~3-6 m/s).

## What's Emulated

### VESC Protocol
- Firmware version (6.05)
- Motor/App config read/write with persistence
- Motor detection (R/L, flux linkage, Hall sensors, apply-all-FOC)
- Real-time values (voltage, current, duty, RPM, temps)
- IMU data (gyro, accelerometer, quaternion)
- BMS battery data (per-cell voltages, SOC, temps)
- CAN ping (reports single controller)
- Terminal commands

### Refloat Package
- Package info and version
- Custom config XML and binary (read/write)
- Real-time data streaming
- QML app UI (served to VESC Tool)
- State machine: startup → ready → running → disengage
- Balance physics: PD controller, tiltback, fault detection

### BLE
- CoreBluetooth peripheral mode (macOS)
- VESC Express NUS (Nordic UART Service) UUIDs
- Flow control with TX queue
- MTU negotiation

## State File

`vescsim_state.json` contains the full MCConf, AppConf, and custom config as human-readable JSON. You can edit it directly and restart the simulator to apply changes.

## Logging

All VESC commands are logged with decoded values:
```
← SET_MCCONF motor_type=2 sensor=0 R=0.0880Ω ...
→ GET_MCCONF R=0.0880Ω L=233.0µH λ=28.0mWb I_max=60.0A cells=21 wheel=280mm
← DETECT_APPLY_ALL_FOC can=false max_power_loss=400.0W ...
→ DETECT_APPLY_ALL_FOC result=0
```

MCConf reads/writes are dumped as full JSON.
