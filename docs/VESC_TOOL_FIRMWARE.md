# VESC Tool Firmware Lookup

## How VESC Tool finds firmware

1. **Download**: On first launch (or manual update), VESC Tool downloads a Qt Resource Container (`.rcc`) from:
   ```
   http://home.vedder.se/vesc_fw_archive/res_fw_{VERSION}.rcc
   ```
   Where `{VERSION}` is the VESC Tool version (e.g. `6.06`). Only the current version is available — old archives are removed.

2. **Cache**: The `.rcc` is saved to the app's data directory and registered as a Qt resource at runtime. On subsequent launches, it loads from cache.

3. **Hardware matching**: When connected to a VESC, the firmware page reads `HW_NAME` from the `COMM_FW_VERSION` response. It then scans `://res/firmwares/` for directories whose name matches the HW name (case-insensitive). Directory names with `_o_` represent multi-hardware builds (e.g. `410_o_411_o_412` matches `410`, `411`, or `412`).

4. **Firmware selection**: Inside each hardware directory, VESC Tool looks for:
   - `vesc_default.bin` — standard firmware
   - `vesc_express.bin` — VESC Express firmware (if applicable)

## Config caching

VESC Tool caches configs per hardware using `hwConfCrc` from the `COMM_FW_VERSION` response:
```
{AppDataLocation}/hw_cache/{hwConfCrc}/
  conf_custom_0.bin     — Refloat custom config XML
  qml_app.bin           — QML app UI (compressed)
  qml_hw.bin            — QML hardware UI (compressed)
```

Changing the `hwConfCrc` forces VESC Tool to re-download all configs and QML.

## COMM_FW_VERSION response format

```
[cmd:1]
[major:1][minor:1]
[hw_name:string\0]
[uuid:12]
[isPaired:1]
[isTestFw:1]
[hwType:1]           — 0=VESC, 3=VESC_Express
[customConfigNum:1]  — number of custom configs (1 for Refloat)
[hasPhaseFilters:1]
[qmlHw:1]            — 0=none, 1=has, 2=fullscreen
[qmlApp:1]           — 0=none, 1=has, 2=fullscreen
[nrfFlags:1]         — bit0=nameSupported, bit1=pinSupported
[fwName:string\0]    — package name (e.g. "Refloat")
[hwConfCrc:4]        — uint32, used for config cache key
```

## HW_NAME values (from FW 6.06 archive)

These are the directory names inside the firmware `.rcc`. The simulator's `HWName` field must match one of these exactly for the firmware update page to work.

### Trampa VESC 6
- `60_MK3`, `60_MK4`, `60_MK5`, `60_MK6`, `60_MK6_HP`, `60_MK6_MAX`
- `60_75`, `60_75_mk2`
- `60v2_alva`, `60v2_alva_mk1`, `60v2_alva_mk2`

### Trampa VESC 75
- `75_100`, `75_100_V2`, `75_300`, `75_300_MKIV`, `75_300_R2`, `75_300_R3`, `75_600`
- `100_250`, `100_250_MKIII`, `100_500`

### Stormcore
- `STORMCORE_60D`, `STORMCORE_60D+`, `STORMCORE_60Dxs`
- `STORMCORE_100D`, `STORMCORE_100DX`, `STORMCORE_100D_V2`, `STORMCORE_100S`

### Ubox
- `UBOX_SINGLE_75`, `UBOX_SINGLE_80`, `UBOX_SINGLE_85_200`, `UBOX_SINGLE_100`
- `UBOX_V1_75_MICRO`, `UBOX_V1_75_TYPEC`, `UBOX_V2_75`, `UBOX_V2_100`

### Float/Onewheel controllers
- `Thor300`, `Thor301`, `Thor400`, `Thor400v2`
- `Little_FOCer`, `Little_FOCer_V3`, `Little_FOCer_V3_1`, `Little_FOCer_V4`

### Other
- `A50S_*`, `A100S_*`, `A200S_*` — Flipsky
- `MKSESC_*` — MakerBase
- `Cheap_FOCer_2`, `Cheap_FOCer_2_V09`
- `UNITY`, `SOLO`, `EDU`, `HD60`
- `LUNA_BBSHD`, `LUNA_M600*` — Luna ebike
- `GO_FOC_*` — Go FOC
- `Raiden7`, `Warrior6`, `Maxim*`, `Minim`, `Pronto`
- `FSESC75300`, `FSESC_75_200_ALU`
- `GESC`, `Duet`, `STR365`, `STR500`
- `RSR_DD_V1`, `RSR_DD_V2`, `RSR_DD_V2.1`
- `JetFleetF6_*` — jet board
- `410_o_411_o_412` — multi-hardware build
