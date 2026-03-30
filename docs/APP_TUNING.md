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

## Tuning UI — Radar Chart

### Why a Radar Chart

Individual sliders hide the big picture. When you adjust "responsiveness" you can't see how it relates to "stability" — but those two things are deeply connected in how a board rides. A radar chart shows the **entire ride personality as a shape**. You see it, you drag it, you feel the result.

Presets become recognizable silhouettes. "Chill" is a small, round shape. "Charge" is a large, spiky star. Your custom tune is whatever shape feels right to *you*. You can overlay profiles to compare them visually.

### The 6 Axes

Each axis of the radar represents a ride characteristic. Each axis maps to **multiple** underlying Refloat parameters — the rider never needs to know which ones.

```
                    Responsiveness
                         ╱╲
                        ╱  ╲
                       ╱    ╲
          Stability ──╱──────╲── Carving
                     ╱ ╲    ╱ ╲
                    ╱   ╲  ╱   ╲
                   ╱     ╲╱     ╲
          Safety ──────────────── Braking
                         │
                         │
                     Agility
```

| Axis | What it Means | What it Controls |
|------|--------------|-----------------|
| **Responsiveness** | How quickly the board reacts to weight shifts | ATR strength, ATR speed boost, ATR response speed, ATR accel/decel limits |
| **Stability** | How planted the board feels at speed | Mahony KP, Mahony KP roll, booster angle, booster ramp, booster current |
| **Carving** | How much the board leans into turns | Turn tilt strength, turn tilt angle limit, turn tilt start angle |
| **Braking** | How aggressively you can decelerate | Brake current, brake tilt strength, brake tilt angle limit |
| **Safety** | How early and strongly the board warns you | Tiltback speed ERPM, tiltback duty, tiltback angle, tiltback ramp speed |
| **Agility** | How nimble at low speed, how quick to engage/disengage | Startup pitch tolerance, startup speed, fault delays, footpad threshold |

### Main Tuning Screen

```
┌─────────────────────────────────┐
│  My Tune              [Charge ▾]│ ← active profile picker
│                                 │
│          Responsiveness          │
│               ●                  │
│              ╱ ╲                 │
│             ╱   ╲                │
│            ╱  ·  ╲               │
│  Stability╱· · · ·╲Carving      │
│           ╲· · · ·╱              │
│            ╲  ·  ╱               │
│             ╲   ╱                │
│              ╲ ╱                 │
│      Safety   ●   Braking       │
│               │                  │
│            Agility               │
│                                 │
│  ┌─────────────────────────┐    │
│  │ Responsiveness    ●━━━━ │ 7  │
│  │ How quickly the board   │    │
│  │ reacts to your shifts   │    │
│  └─────────────────────────┘    │
│                                 │
│  [Apply]  [Save]  [Advanced ▸]  │
│                                 │
└─────────────────────────────────┘
```

### Interaction Model

1. **Drag a vertex** on the radar chart → that axis value changes, shape updates in real-time
2. The **detail card** below the chart shows the currently-selected axis with:
   - Plain-language name and description
   - Numeric value (1-10 scale)
   - A one-line summary of the consequence: "Pushback starts at ~32 mph"
3. Tap a different vertex or axis label → detail card switches to that axis
4. **Pinch** the entire shape to scale all axes proportionally (make everything more/less)
5. Shape updates are **live** — applied to the board immediately so the rider can feel it
6. "Save" persists to flash. "Apply" writes without persisting (experiment mode).

### Preset Shapes

Each preset has a distinctive silhouette:

```
Chill                Flow                 Charge               Trail
  ·                    ·                    ●                    ·
 ╱ ╲                  ╱ ╲                  ╱ ╲                  ╱ ╲
·   ·                ●   ●                ●   ●                ·   ●
 ╲ ╱                  ╲ ╱                  ╲ ╱                  ╲ ╱
  ·                    ·                    ●                    ·
  ·                    ·                    ·                    ●

Small, round         Balanced pentagon    Large, aggressive    Tall on agility
Low everything       Medium everything    High everything      + stability
                                          except safety
```

When you select a preset, the radar shape **animates** from your current tune to the preset shape. This makes it viscerally clear what's changing.

### Overlay Comparison

Long-press a preset button to overlay its shape on top of your current tune:

```
         Responsiveness
              ●╌╌╌╌○          ● = your tune (solid, teal)
             ╱╲   ╱╲          ○ = preset overlay (dashed, gray)
            ╱  ╲ ╱  ╲
  Stability●  ╌○╌╌╌╌○Carving
            ╲  ╱ ╲  ╱
             ╲╱   ╲╱
     Safety   ●╌╌╌╌○  Braking
              │
           Agility
```

This lets riders see: "Charge has way more responsiveness and braking than my tune, but less safety margin." They can then drag individual vertices toward the preset shape without adopting it wholesale.

### Detail Card Behavior

Tapping an axis shows a detail card with context-aware information:

**Safety axis selected:**
```
┌─────────────────────────────────┐
│ Safety                    ●━━━━ │ 4/10
│                                 │
│ When the board pushes back to   │
│ warn you about speed limits.    │
│                                 │
│ Pushback starts at ~32 mph      │
│ Duty limit: 85%                 │
│ Pushback angle: 6°              │
│                                 │
│ ⚠ Lower values mean less        │
│   warning before the motor's    │
│   physical limits.              │
└─────────────────────────────────┘
```

**Responsiveness axis selected:**
```
┌─────────────────────────────────┐
│ Responsiveness            ━━●━━ │ 7/10
│                                 │
│ How quickly the board reacts    │
│ to your weight shifts.          │
│                                 │
│ ATR: strong                     │
│ Speed boost: moderate           │
│                                 │
│ Higher = snappier reactions,    │
│ but can feel twitchy if you're  │
│ not used to it.                 │
└─────────────────────────────────┘
```

### Axis Value Scale

Each axis uses a 1-10 integer scale. This is what the rider sees and thinks in. Internally, each integer maps to a curated set of Refloat parameter values.

| Axis | 1 (min) | 5 (mid) | 10 (max) |
|------|---------|---------|----------|
| **Responsiveness** | ATR 0.3, boost 0 | ATR 1.0, boost 0.5 | ATR 2.5, boost 2.0 |
| **Stability** | KP 0.4, boost 0A | KP 1.5, boost 5A | KP 3.0, boost 15A |
| **Carving** | Tilt 1.0, limit 4° | Tilt 5.0, limit 8° | Tilt 12.0, limit 15° |
| **Braking** | 6A, tilt 2.0 | 15A, tilt 5.0 | 30A, tilt 10.0 |
| **Safety** | ERPM 30000, 10° tilt | ERPM 22000, 7° tilt | ERPM 15000, 4° tilt |
| **Agility** | 20° tol, 300ms delay | 15° tol, 150ms delay | 8° tol, 50ms delay |

Note: Safety is **inverted** — higher values mean *more* safety (earlier warnings), lower values mean less safety (later warnings, closer to limits). This is deliberate: dragging the safety vertex outward should make the board *safer*, not less safe.

The mapping between scale values is interpolated along a curated curve. Values between integers are valid — the slider is continuous, the 1-10 labels are just reference points.

### Startup & Disengage (Separate Section)

Startup and disengage settings affect safety in ways that don't fit neatly on the radar. These get their own small section below the chart:

```
┌─────────────────────────────────┐
│ Startup & Disengage             │
│                                 │
│ Footpad sensitivity   ━━━●━━━━━ │
│ How firmly you need to press    │
│                                 │
│ Disengage speed       ━━━━●━━━━ │
│ How quickly the board stops     │
│ when you lift a foot            │
│                                 │
│ These affect safety. Change     │
│ carefully and test at low speed.│
└─────────────────────────────────┘
```

---

## Advanced Mode

Tapping "Advanced" opens the full parameter editor, organized by radar axis. Each parameter shows which axis it belongs to and its current value.

```
▼ Responsiveness (axis value: 7)
  ATR Strength          [  1.2  ] ← direct numeric input
  ATR Speed Boost       [  0.8  ]
  ATR Response Speed    [  1.0  ]
  ATR Accel Limit       [ 10.0  ]
  ATR Decel Limit       [ 10.0  ]

▼ Stability (axis value: 5)
  Mahony KP             [  1.5  ]
  Mahony KP Roll        [  0.3  ]
  Booster Angle         [ 10.0  ]
  Booster Ramp          [  5.0  ]
  Booster Current       [ 10.0A ]

▼ Carving (axis value: 6)
  Turn Tilt Strength    [  5.0  ]
  Turn Tilt Angle Limit [  8.0° ]
  Turn Tilt Start Angle [  2.0° ]

▼ Braking (axis value: 6)
  Brake Current         [ 15.0A ]
  Brake Tilt Strength   [  5.0  ]
  Brake Tilt Angle Limit[ 10.0° ]

▼ Safety (axis value: 4)
  Tiltback Speed (ERPM) [ 25000 ]  (~32 mph)
  Tiltback Duty         [  0.85 ]
  Tiltback Angle        [  8.0° ]
  Tiltback Ramp Speed   [  3.0  ]

▼ Agility (axis value: 5)
  Startup Pitch Tol.    [ 15.0° ]
  Startup Speed         [  0.5  ]
  Fault Delay Pitch     [ 250ms ]
  Fault Delay Half SW   [ 100ms ]
  Fault Delay Full SW   [ 250ms ]
  Footpad Threshold     [  1.5V ]
```

When individual parameters are edited in advanced mode, the radar chart axis value updates to reflect the change (it may become a non-integer). A dot appears on the radar to indicate "custom — not on the standard curve." This makes it clear when the rider has gone off-script.

Each parameter shows:
- Current value (editable)
- Default value (tap to reset)
- Min/max safe range
- Brief tooltip explaining what it does

---

## Live Preview

When the board is connected, changes are applied in real-time:

- **Radar vertex drag**: immediately writes all affected parameters via `COMM_SET_CUSTOM_CONFIG` (95)
- **ATR changes**: rider can feel the difference on the next push
- **Tiltback changes**: take effect on next ride engage
- **Startup changes**: feel on next footpad step-on

Changes are applied live but **not persisted** until the user taps "Save to Board" (`COMMAND_CFG_SAVE` (4)). This lets riders experiment without committing. If the board reboots or disconnects, it reverts to the last saved config.

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
