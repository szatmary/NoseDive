# NoseDive — Rider Profiles & Tuning

## Overview

After the setup wizard configures the hardware, the rider needs to dial in how the board *feels*. Different riders have different preferences, and even the same rider wants different behavior depending on context (commuting vs trails vs showing off).

NoseDive uses a **rider profile** system that sits on top of the hardware configuration. A rider profile is a named collection of Refloat tune settings that can be swapped on the fly without touching the underlying motor, battery, or IMU config.

```
┌──────────────────────────────────────────┐
│              Board Hardware               │
│  (MC_CONF, motor detection, IMU, battery) │
│  ─── Set once by wizard, rarely changed ──│
├──────────────────────────────────────────┤
│           Rider Profile                   │
│  (Refloat tune, feel, safety limits)      │
│  ─── Swapped freely per rider/context ────│
└──────────────────────────────────────────┘
```

---

## Rider Profiles

### What's in a Profile

A rider profile controls how the board responds to the rider. It does **not** include hardware settings (motor params, battery cutoffs, wheel size) — those belong to the board profile from the setup wizard.

| Category | Settings | What it Affects |
|----------|----------|-----------------|
| **Feel** | ATR strength, turn tilt, speed boost | How responsive and nimble the board feels |
| **Stability** | Mahony KP, balance filter | How planted vs loose at speed |
| **Pushback** | Speed tiltback, duty tiltback, angle | When and how hard the board warns you |
| **Braking** | Brake curve, brake tilt | How aggressively the board decelerates |
| **Startup** | Footpad sensitivity, startup angle, startup speed | How easy it is to engage/disengage |
| **Nose Behavior** | Tiltback voltage, tiltback temp | Safety pushback triggers |
| **Remote** | Remote input curve, deadband | Remote feel (if using one) |

### Profile Storage

```json
{
  "name": "Daily Commute",
  "icon": "road",
  "author": "rider",
  "created": "2026-03-15",
  "modified": "2026-03-28",
  "base_preset": "intermediate",
  "settings": {
    "feel": {
      "atr_strength": 1.2,
      "atr_speed_boost": 0.8,
      "turn_tilt_strength": 5.0,
      "turn_tilt_angle_limit": 8.0
    },
    "stability": {
      "mahony_kp": 1.5,
      "mahony_kp_roll": 0.3
    },
    "pushback": {
      "tiltback_speed_erpm": 25000,
      "tiltback_duty": 0.85,
      "tiltback_angle": 8.0,
      "tiltback_speed": 3.0
    },
    "braking": {
      "brake_current": 15.0,
      "brake_tilt_strength": 5.0,
      "brake_tilt_angle_limit": 10.0
    },
    "startup": {
      "footpad_sensor_threshold": 1.5,
      "startup_pitch_tolerance": 15.0,
      "startup_speed": 0.5,
      "fault_delay_pitch": 250,
      "fault_delay_switch_half": 100,
      "fault_delay_switch_full": 250
    }
  }
}
```

---

## Built-in Presets

The app ships with curated presets that serve as starting points. Users can't modify built-in presets directly — they duplicate into a custom profile first.

### Chill

*For beginners or relaxed cruising. Forgiving, slow to react, gentle pushback early.*

| Setting | Value | Why |
|---------|-------|-----|
| ATR strength | 0.5 | Less responsive, more predictable |
| Speed boost | 0.0 | No speed-dependent nose rise |
| Tiltback speed | Low (20 mph equivalent) | Warns early |
| Tiltback angle | 10° | Strong, obvious pushback |
| Brake tilt | 3.0 | Gentle braking |
| Startup tolerance | 20° | Easy to engage |
| Fault delay (half switch) | 300ms | Slow to disengage — forgiving |

### Flow

*Balanced everyday riding. Good for commuting and casual carving.*

| Setting | Value | Why |
|---------|-------|-----|
| ATR strength | 1.0 | Responsive but predictable |
| Speed boost | 0.5 | Mild nose rise at speed |
| Turn tilt | 5.0 | Natural lean into turns |
| Tiltback speed | Medium (28 mph equivalent) | Room to ride before warning |
| Tiltback angle | 8° | Noticeable but not harsh |
| Brake tilt | 5.0 | Firm braking |
| Startup tolerance | 15° | Standard |

### Charge

*Aggressive riding. Quick reactions, high limits, maximum performance.*

| Setting | Value | Why |
|---------|-------|-----|
| ATR strength | 2.0 | Very responsive, immediate torque |
| Speed boost | 1.5 | Significant nose rise at speed |
| Turn tilt | 8.0 | Aggressive carving |
| Tiltback speed | High (35 mph equivalent) | Late warning for experienced riders |
| Tiltback angle | 5° | Subtle — rider manages limits |
| Brake tilt | 8.0 | Hard braking available |
| Startup tolerance | 10° | Quick engage |
| Fault delay (half switch) | 50ms | Fast disengage — requires precision |

### Trail

*Off-road and rough terrain. Looser feel, more body movement tolerance.*

| Setting | Value | Why |
|---------|-------|-----|
| ATR strength | 1.5 | Responsive for terrain changes |
| Mahony KP | 0.8 | Looser balance — absorbs bumps |
| Turn tilt | 3.0 | Less aggressive — uneven surfaces |
| Tiltback speed | Medium-low | Trails are slower |
| Brake tilt | 4.0 | Moderate — terrain varies |
| Startup tolerance | 20° | Easy engage on slopes |
| Fault delay (half switch) | 250ms | Forgiving — bumps can lift feet |

---

## Tuning UI

### Main Tuning Screen

The tuning screen is organized by **what the rider cares about**, not by Refloat parameter names. Each section has a plain-language description and a visual indicator.

```
┌─────────────────────────────────┐
│  Active Profile: Daily Commute  │
│  [Chill] [Flow] [Charge] [Trail]│ ← preset quick-switch
├─────────────────────────────────┤
│                                 │
│  ◉ Responsiveness         ━━━━●│ ← simple slider
│    How quickly the board reacts │
│    to your movements            │
│                                 │
│  ◉ Top Speed Warning      ━●━━━│
│    When pushback kicks in       │
│    ~28 mph                      │
│                                 │
│  ◉ Carving Feel           ━━●━━│
│    How much the board leans     │
│    into turns                   │
│                                 │
│  ◉ Braking Strength       ━━━●━│
│    How aggressively you can     │
│    slow down                    │
│                                 │
│  ◉ Stability at Speed     ━━●━━│
│    Tighter = more planted,      │
│    Looser = more nimble         │
│                                 │
│  [Advanced Settings ▸]         │
│                                 │
│  [Save to Board]               │
└─────────────────────────────────┘
```

### Simple Mode (Default)

Five primary sliders that each map to multiple underlying Refloat parameters:

| Slider | Maps To | Range |
|--------|---------|-------|
| **Responsiveness** | ATR strength, ATR speed boost, balance filter | Low (mellow) → High (snappy) |
| **Top Speed Warning** | Tiltback speed ERPM, tiltback duty, tiltback angle | Early (safe) → Late (expert) |
| **Carving Feel** | Turn tilt strength, turn tilt angle limit | Minimal → Aggressive |
| **Braking Strength** | Brake current, brake tilt strength, brake tilt angle limit | Gentle → Hard |
| **Stability at Speed** | Mahony KP, Mahony KP roll, booster parameters | Loose (nimble) → Tight (planted) |

Each slider position is interpolated between known-good value combinations. The mapping is non-linear — the middle positions are the sweet spot for most riders, and extremes require intent.

### Advanced Mode

Tapping "Advanced Settings" expands each category to show the individual Refloat parameters:

```
▼ Responsiveness
  ATR Strength          [  1.2  ] ← direct numeric input
  ATR Speed Boost       [  0.8  ]
  ATR Response Speed    [  1.0  ]
  ATR Accel Limit       [ 10.0  ]
  ATR Decel Limit       [ 10.0  ]

▼ Top Speed Warning
  Tiltback Speed (ERPM) [ 25000 ]
  Tiltback Duty         [  0.85 ]
  Tiltback Angle        [  8.0° ]
  Tiltback Ramp Speed   [  3.0  ]

▼ Carving
  Turn Tilt Strength    [  5.0  ]
  Turn Tilt Angle Limit [  8.0° ]
  Turn Tilt Start Angle [  2.0° ]

▼ Braking
  Brake Current         [ 15.0A ]
  Brake Tilt Strength   [  5.0  ]
  Brake Tilt Angle Limit[ 10.0° ]

▼ Stability
  Mahony KP             [  1.5  ]
  Mahony KP Roll        [  0.3  ]
  Booster Angle         [ 10.0  ]
  Booster Ramp          [  5.0  ]
  Booster Current       [ 10.0A ]

▼ Startup & Disengage
  Footpad Threshold     [  1.5V ]
  Startup Pitch Tol.    [ 15.0° ]
  Startup Speed         [  0.5  ]
  Fault Delay Pitch     [ 250ms ]
  Fault Delay Half SW   [ 100ms ]
  Fault Delay Full SW   [ 250ms ]
```

Each parameter shows:
- Current value (editable)
- Default value (tap to reset)
- Min/max safe range
- Brief tooltip explaining what it does

### Live Preview

When the board is connected, certain changes can be previewed in real-time:

- **ATR changes**: apply immediately via `COMM_SET_CUSTOM_CONFIG` (95) — rider can feel the difference on the next push
- **Tiltback changes**: take effect on next ride engage
- **Startup changes**: feel on next footpad step-on

Changes are applied live but **not persisted** until the user taps "Save to Board" (`COMMAND_CFG_SAVE` (4)). This lets riders experiment without committing.

---

## Speed Display Integration

Sliders that relate to speed show the value in the rider's preferred unit:

- **Tiltback Speed ERPM → "~28 mph"** using the board profile's wheel circumference and pole pairs
- Conversion: `speed_mph = erpm / erpm_per_mps / 0.44704`
- If wheel calibration was done (Step 8), this is accurate
- If not, it's estimated from nominal wheel diameter

The speed conversion is one of the main reasons the board profile (wheel diameter, pole pairs) is set up before tuning.

---

## Profile Switching

### Quick Switch

The top of the tuning screen shows preset buttons. Tapping one:

1. Confirms: "Switch to [Charge]? Current unsaved changes will be lost."
2. Loads the profile's settings
3. Writes to board via `COMM_SET_CUSTOM_CONFIG` (95)
4. Saves persistently via `COMMAND_CFG_SAVE` (4)
5. Board is immediately rideable with new tune

### Per-Rider Profiles

If multiple people ride the same board:

1. Each rider has their own named profile
2. On the board's main screen: "Rider: Alex [switch]"
3. Switching riders loads their profile and writes to the board
4. Each rider's profile stores their own:
   - Tune settings
   - Preferred units (mph/kph)
   - Dashboard layout
   - Ride history

### Profile Sharing

Riders can share profiles:

- **Export**: generate a shareable link or QR code containing the profile JSON
- **Import**: scan QR or open link, preview settings, apply to board
- **Community**: future feature — browse and rate community-shared profiles
- Imported profiles are always duplicated (never overwrite existing)

---

## Tuning History

Every time a profile is saved to the board, the app logs the change:

```
March 30 — "Daily Commute" saved
  Changed: ATR strength 1.0 → 1.2, turn tilt 4.0 → 5.0
March 28 — "Daily Commute" saved
  Changed: tiltback speed 24000 → 25000
March 25 — Switched to "Charge" profile
March 20 — "Daily Commute" created from "Flow" preset
```

This lets riders:
- See what they changed and when
- Revert to a previous version if they don't like a change
- Correlate tune changes with ride experience

---

## Danger Zone / MC_CONF Access

Most riders never need to touch MC_CONF. But for advanced users and builders:

### Accessible via Settings → Advanced → Motor Configuration

Behind a warning gate: *"These settings affect motor control at a low level. Incorrect values can damage hardware or cause dangerous behavior. Only modify if you understand what you're doing."*

Exposed MC_CONF parameters (grouped):

**Current Limits**
- Motor current max / min (brake)
- Battery current max / min (regen)
- Absolute max current

**FOC Parameters**
- Motor resistance, inductance, flux (read-only — set by detection)
- Observer gain
- Current controller bandwidth
- Openloop ERPM / current

**Temperature Limits**
- MOSFET temp cutoff start / end
- Motor temp cutoff start / end

**Battery**
- Voltage cutoff start / end (also settable in wizard Step 7)
- Battery type

These are written via `COMM_SET_MC_CONF` (13) and are **board-level** settings (not per-rider).

---

## Tuning Data Flow

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  Built-in    │────►│  Rider       │────►│  Board       │
│  Presets     │     │  Profile     │     │  (Refloat)   │
│  (read-only) │     │  (on device) │     │  (live)      │
└─────────────┘     └─────────────┘     └─────────────┘
                          │                     │
                          │    COMM_SET_         │
                          │    CUSTOM_CONFIG     │
                          │───────────────────►  │
                          │                     │
                          │    COMMAND_          │
                          │    CFG_SAVE          │
                          │───────────────────►  │
                          │                     │
                     ┌────┴────┐                │
                     │ History  │                │
                     │ Log      │                │
                     └─────────┘                │
```

1. User picks a preset or creates a custom profile
2. Adjusts sliders (simple) or parameters (advanced)
3. Changes are written live to the board (not persisted)
4. User taps "Save to Board" → persists to flash
5. Change is logged in tuning history
