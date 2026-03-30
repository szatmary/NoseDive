# NoseDive — App Vision & Design

## The Problem

Today's onewheel VESC rider juggles 3-5 apps:

| App | What it Does | Problem |
|-----|-------------|---------|
| VESC Tool | Motor config, firmware updates | Desktop UI crammed onto phone. Exposes every parameter. Terrifying for new riders. |
| Float Control | Refloat tuning | Functional but ugly. Still too many knobs. |
| Float Hub Tool | Refloat tuning + telemetry | Better, but separate from VESC config. Can't do firmware or motor detection. |
| BMS app | Battery monitoring | Yet another app. Usually Chinese with bad translations. |
| Strava / ride tracker | GPS routes, stats | Doesn't know anything about the board. |

The rider experience is fragmented. Setting up a new board means bouncing between apps, watching YouTube tutorials, and asking Discord for help. Tuning means understanding 30+ parameters with names like `mahony_kp_roll`.

## The Vision

**NoseDive is the only app a VESC onewheel rider needs.**

Three principles:

### 1. All-in-One

One app replaces everything:

- Firmware updates (VESC Express, VESC, Refloat, BMS)
- Motor detection and hardware setup
- Tuning and rider profiles
- Real-time telemetry dashboard
- Ride tracking with GPS
- Battery and BMS monitoring
- Trip history and lifetime stats
- Board management (multiple boards)
- Community (share profiles, group rides)

No reason to open another app. Ever.

### 2. Professional Quality

NoseDive should look like it was made by a funded startup, not a hobby project. Design references:

- **Strava** — clean data presentation, satisfying ride summaries
- **Apple Health** — card-based layout, progressive disclosure
- **Tesla app** — device control that feels premium
- **Peloton** — fitness + community done right

Specifics:
- Native UI on each platform (SwiftUI on iOS, Jetpack Compose on Android)
- Consistent design language, no janky webviews
- Smooth animations and transitions
- Dark mode default (riders are often outdoors at night)
- Typography and spacing that breathes
- Custom illustrations for wizard steps, not stock icons
- Haptic feedback on iOS for confirmations and warnings
- No exposed engineering jargon in the default UI

### 3. Simple "Just Works" Tuning

The biggest differentiator. Tuning should feel like adjusting the seat in a car, not programming a PLC.

**The core insight**: 95% of riders want the same 5 things dialed in. The other 30+ Refloat parameters exist for edge cases and hardware differences. The app should handle the complexity internally and present simple choices externally.

---

## App Structure

### Tab Bar

```
┌─────┬─────┬─────┬─────┬─────┐
│ 🏠  │  📊 │  🎯 │  🗺  │  👤 │
│Home │Dash │Tune │Rides│ Me  │
└─────┴─────┴─────┴─────┴─────┘
```

Five tabs. Nothing hidden behind hamburger menus. Each tab has a clear purpose.

---

## Tab 1: Home

The landing screen. Shows the connected board at a glance.

### Connected State

```
┌─────────────────────────────────┐
│                                 │
│     ┌───────────────────┐       │
│     │                   │       │
│     │   [Board Image]   │       │
│     │                   │       │
│     └───────────────────┘       │
│                                 │
│     Funwheel X7 Long Range      │
│     Rider: Alex                 │
│                                 │
│  ┌─────────┐  ┌─────────┐      │
│  │ 72.3V   │  │ 28°C    │      │
│  │ ████░░  │  │ Motor   │      │
│  │ 68%     │  │ Temp    │      │
│  └─────────┘  └─────────┘      │
│                                 │
│  ┌─────────┐  ┌─────────┐      │
│  │ 1,247   │  │ Flow    │      │
│  │ miles   │  │ Profile │      │
│  │ total   │  │ Active  │      │
│  └─────────┘  └─────────┘      │
│                                 │
│  ┌─────────────────────────┐    │
│  │  Quick Actions           │    │
│  │  [Start Ride] [Tune]    │    │
│  │  [Switch Rider] [More]  │    │
│  └─────────────────────────┘    │
│                                 │
└─────────────────────────────────┘
```

### Disconnected State

```
┌─────────────────────────────────┐
│                                 │
│  Your Boards                    │
│                                 │
│  ┌─────────────────────────┐    │
│  │ Funwheel X7 LR          │    │
│  │ Last connected: 2h ago   │    │
│  │ [Connect]                │    │
│  └─────────────────────────┘    │
│                                 │
│  ┌─────────────────────────┐    │
│  │ DIY Trail Board          │    │
│  │ Last connected: 3d ago   │    │
│  │ [Connect]                │    │
│  └─────────────────────────┘    │
│                                 │
│  [+ Add New Board]              │
│  [Scan for Boards]              │
│                                 │
└─────────────────────────────────┘
```

---

## Tab 2: Dashboard

Real-time telemetry while riding. Designed to be glanceable at speed — large numbers, high contrast, minimal text.

### Ride Mode (Active)

```
┌─────────────────────────────────┐
│                                 │
│            18.3                  │
│            mph                   │
│                                 │
│  ┌──────────────────────────┐   │
│  │ ░░░░░░░░░░████████░░░░░░ │   │
│  │         duty: 42%         │   │
│  └──────────────────────────┘   │
│                                 │
│   68%        31°C       2.4mi   │
│   battery    motor      trip    │
│                                 │
│  ┌──────────────────────────┐   │
│  │      pitch: -1.2°        │   │
│  │  ◄── balance gauge ──►   │   │
│  └──────────────────────────┘   │
│                                 │
│  ┌─────┐ ┌─────┐ ┌─────┐      │
│  │12.3A│ │ 842W│ │24.1A│      │
│  │motor│ │power│ │batt │      │
│  └─────┘ └─────┘ └─────┘      │
│                                 │
└─────────────────────────────────┘
```

### Design Notes

- Speed is the hero element — largest text on screen
- Duty cycle bar is the most important safety indicator (how hard the motor is working)
- Color coding: duty bar shifts green → yellow → orange → red as it approaches limits
- Battery percentage uses the SOC curve from the board profile
- Screen stays awake during active ride
- Landscape mode supported — rearranges to horizontal gauge layout

### Customizable Dashboard

Long-press any metric to swap it. Available widgets:

| Widget | Shows |
|--------|-------|
| Speed | Current speed in mph/kph |
| Duty | Motor duty cycle % with color bar |
| Battery | Voltage, percentage, cell bar graph |
| Motor Temp | Temperature with warning threshold |
| MOSFET Temp | Controller temperature |
| Power | Instantaneous watts |
| Motor Current | Amps to motor |
| Battery Current | Amps from battery |
| Trip Distance | Current ride distance |
| Pitch | Board angle (balance indicator) |
| Roll | Side-to-side tilt |
| Footpad | Sensor state (none/half/full) |
| ERPM | Raw motor electrical RPM |
| Setpoint | Target pitch angle |
| ATR | Active ATR contribution |
| Efficiency | Wh/mile for current ride |

---

## Tab 3: Tune

The tuning experience from `APP_TUNING.md`, but refined for "just works."

### First-Time Flow

When a rider first opens the Tune tab:

```
┌─────────────────────────────────┐
│                                 │
│  How do you ride?               │
│                                 │
│  Pick a starting point.         │
│  You can always adjust later.   │
│                                 │
│  ┌─────────────────────────┐    │
│  │ 🌊 Chill                │    │
│  │ Relaxed cruising.        │    │
│  │ Forgiving and mellow.    │    │
│  │ Great for learning.      │    │
│  └─────────────────────────┘    │
│                                 │
│  ┌─────────────────────────┐    │
│  │ 🔄 Flow                 │    │
│  │ Daily riding.            │    │
│  │ Balanced and smooth.     │    │
│  │ Good for everything.     │    │
│  └─────────────────────────┘    │
│                                 │
│  ┌─────────────────────────┐    │
│  │ ⚡ Charge                │    │
│  │ Aggressive performance.  │    │
│  │ Quick and responsive.    │    │
│  │ For experienced riders.  │    │
│  └─────────────────────────┘    │
│                                 │
│  ┌─────────────────────────┐    │
│  │ 🌲 Trail                │    │
│  │ Off-road and trails.     │    │
│  │ Loose and forgiving.     │    │
│  │ Absorbs rough terrain.   │    │
│  └─────────────────────────┘    │
│                                 │
└─────────────────────────────────┘
```

Rider picks one. Done. The board is rideable. No parameter tweaking required.

### Post-Selection

If the rider wants to adjust:

```
┌─────────────────────────────────┐
│  Flow                    [Edit] │
│                                 │
│  Responsiveness       ━━━━●━━━━ │
│  The board reacts quickly to    │
│  your movements.                │
│                                 │
│  Top Speed Warning    ━━●━━━━━━ │
│  Pushback starts at ~28 mph.    │
│  Move right to push higher.     │
│                                 │
│  Carving              ━━━●━━━━━ │
│  How much the board leans       │
│  into your turns.               │
│                                 │
│  Braking              ━━━━●━━━━ │
│  How hard you can slow down.    │
│                                 │
│  Stability            ━━━●━━━━━ │
│  Tighter = planted at speed.    │
│  Looser = nimble in turns.      │
│                                 │
│  [Save]                         │
│                                 │
│  [Advanced ▸]   [Reset to Flow] │
│                                 │
└─────────────────────────────────┘
```

Each slider has:
- A human-readable label (not `mahony_kp`)
- A one-line description of what changes
- A computed real-world value where applicable ("~28 mph")
- A position relative to the base preset

**The key UX detail**: moving a slider gives immediate feedback. The text below it updates: "Pushback starts at ~28 mph" → "Pushback starts at ~32 mph". The rider understands the consequence without knowing what ERPM means.

### "Just Works" Philosophy

| What the rider sees | What actually happens |
|--------------------|-----------------------|
| Responsiveness slider at 60% | ATR strength = 1.2, ATR speed boost = 0.8, ATR response speed = 1.0 |
| Top Speed Warning slider at 40% | Tiltback ERPM = 25000, tiltback duty = 0.85, tiltback angle = 8° |
| Carving slider at 50% | Turn tilt strength = 5.0, turn tilt angle limit = 8°, turn tilt start angle = 2° |
| "Save" button | COMM_SET_CUSTOM_CONFIG → COMMAND_CFG_SAVE |

The mapping between slider positions and parameter values is a **curated curve**, not a linear interpolation. The curves are tuned so that:
- The middle 60% of slider range covers the settings 90% of riders want
- The extremes are available but require deliberate movement
- No slider position produces an unsafe combination
- Moving one slider never requires adjusting another to compensate

---

## Tab 4: Rides

GPS-tracked ride history with stats.

### Ride Recording

Automatic when the board is connected and moving:
1. Board connects → app enters "ready" state
2. First footpad engagement + movement → recording starts
3. GPS tracking begins (battery-efficient mode)
4. Telemetry sampled at 10Hz (speed, duty, current, pitch, battery)
5. Footpad disengage + no movement for 30s → recording pauses
6. BLE disconnect or manual stop → ride saved

### Ride Summary

```
┌─────────────────────────────────┐
│                                 │
│  Saturday Afternoon Ride        │
│  March 30, 2026                 │
│                                 │
│  ┌──────────────────────────┐   │
│  │                          │   │
│  │      [Map View]          │   │
│  │      GPS route overlay   │   │
│  │                          │   │
│  └──────────────────────────┘   │
│                                 │
│  12.4 mi    42 min    17.8 mph  │
│  distance   duration  avg speed │
│                                 │
│  31.2 mph   847 Wh    68 Wh/mi │
│  top speed  energy    efficiency│
│                                 │
│  ┌──────────────────────────┐   │
│  │ Speed ──────────────      │   │
│  │ Duty  ▁▂▃▅▇▅▃▂▁▂▃▅▇     │   │
│  │ Batt  ━━━━━━━━━━━━━━━━━  │   │
│  │        0    10    20  42m │   │
│  └──────────────────────────┘   │
│                                 │
│  Battery: 92% → 54% (-38%)     │
│  Profile: Flow                  │
│  Board: Funwheel X7 LR         │
│                                 │
│  [Share]  [Export GPX]          │
│                                 │
└─────────────────────────────────┘
```

### Ride List

```
┌─────────────────────────────────┐
│  Rides                          │
│                                 │
│  This Week              48.2 mi │
│                                 │
│  ┌─────────────────────────┐    │
│  │ Sat Mar 30    12.4 mi   │    │
│  │ 42 min  17.8 avg  Flow  │    │
│  └─────────────────────────┘    │
│  ┌─────────────────────────┐    │
│  │ Thu Mar 28     8.1 mi   │    │
│  │ 28 min  16.2 avg  Flow  │    │
│  └─────────────────────────┘    │
│  ┌─────────────────────────┐    │
│  │ Tue Mar 25    15.3 mi   │    │
│  │ 51 min  18.0 avg  Charge│    │
│  └─────────────────────────┘    │
│                                 │
│  March 2026            187.4 mi │
│  Lifetime            1,247.3 mi │
│                                 │
└─────────────────────────────────┘
```

### Stats & Trends

Weekly / monthly / all-time breakdowns:

- Total distance, ride count, ride time
- Average and max speed trends
- Energy consumption trends (Wh/mi improving?)
- Battery cycle count estimate
- Duty histogram (how often are you near the limit?)
- Favorite routes (GPS clustering)

### Ride Data Storage

- Telemetry stored locally in a compact binary format
- GPS track stored as compressed coordinates
- Synced to cloud for backup / cross-device access
- Export as GPX (for Strava import), CSV (for analysis)

---

## Tab 5: Me

User profile, board management, settings.

### Sections

**My Boards**
- List of registered boards with status
- Tap to see board details, edit profile, view backup history
- Add new board → launches wizard

**Rider Profiles**
- Manage rider profiles for all boards
- Create / edit / delete / duplicate
- Import / export / share

**Settings**
- Units (mph / kph, °F / °C, miles / km)
- Dashboard layout preferences
- Notification preferences (low battery alert, firmware update available)
- BLE settings (auto-connect, background connection)
- Cloud sync on/off
- Dark / light mode (default: dark)
- Haptic feedback on/off

**About**
- App version
- Open source licenses
- Links: community, support, GitHub

---

## Design Language

### Color Palette

```
Background:     #FCFCFC (clean white)
Surface:        #FFFFFF (card backgrounds)
Surface Raised: #F2F2F2 (elevated cards)
Primary:        Hot Pink — Display P3 (1.0, 0.05, 0.4) — HDR on supported displays
Warning:        Vivid Orange — Display P3 (1.0, 0.35, 0.0)
Danger:         Vivid Red — Display P3 (1.0, 0.1, 0.1)
Success:        Vivid Green — Display P3 (0.0, 0.75, 0.35)
Text Primary:   #1A1A1A
Text Secondary: #6E6E6E
Text Tertiary:  #BBBBBB
```

Colors use the Display P3 wide color gamut to push into HDR on
ProMotion and XDR displays. On standard sRGB screens they gracefully
fall back to the nearest representable value.

### Typography

- Hero numbers (speed, voltage): SF Pro Display / Roboto, 72pt, bold
- Card values: 28pt, semibold
- Labels: 14pt, regular, secondary color
- Body text: 16pt, regular
- All caps avoided except for very short labels (MPH, RPM)

### Card Design

Every piece of information lives on a card with:
- 12px corner radius
- Surface background (white)
- 16px internal padding
- Subtle shadow on raised cards
- No borders — elevation implies separation

### Animations

- Tab switches: cross-dissolve (not slide)
- Card appear: fade up from 8px below, 200ms
- Value changes: numeric counter animation (digits roll)
- Connection state: pulsing glow on board image
- Slider movement: haptic tick on iOS at detent points
- Gauge needles: spring physics (slight overshoot and settle)

### Illustrations

Custom illustrations for:
- Setup wizard steps (board on stand, board on ground, wheel spinning)
- Empty states (no boards, no rides)
- Error states (BLE disconnected, update failed)
- Achievement moments (first ride, 100 miles, 1000 miles)

Style: minimal line art, monochrome with hot pink accent. Not cartoonish. Think technical illustration meets Apple keynote aesthetic.

---

## Notification System

### Push Notifications

| Trigger | Message | When |
|---------|---------|------|
| Low battery | "Board at 15%. ~3 miles remaining." | Below 20%, 10%, 5% |
| Firmware available | "Refloat 2.1 is available for your X7." | On app open, once per version |
| Ride milestone | "You hit 1,000 lifetime miles!" | At milestones |
| Temperature warning | "Motor temp 85°C — consider slowing down." | Above warning threshold |

### In-App Alerts

- Duty approaching limit → dashboard turns amber, subtle vibration
- BLE connection lost → top banner with reconnect button
- Config not saved → "Unsaved changes" badge on Tune tab

---

## Offline Capability

The app should work fully offline except for:
- Firmware downloads (needs network, but can use cached/bundled)
- Cloud sync
- Community features

Everything else works without internet:
- BLE connection and board communication
- Ride recording (GPS works offline)
- Tuning and profile management
- Telemetry dashboard
- Local ride history

---

## Competitive Landscape

| Feature | VESC Tool | Float Control | Float Hub | NoseDive |
|---------|-----------|--------------|-----------|----------|
| Firmware updates | Yes | No | No | Yes |
| Motor detection | Yes | No | No | Yes |
| Refloat tuning | Partial | Yes | Yes | Yes |
| Simple tuning | No | No | Partial | **Yes** |
| BMS monitoring | Partial | No | No | Yes |
| Ride tracking | No | No | No | **Yes** |
| GPS routes | No | No | No | **Yes** |
| Multi-board | No | No | Partial | **Yes** |
| Rider profiles | No | No | No | **Yes** |
| Native UI | No (Qt) | Yes | Yes | **Yes** |
| Pro design | No | No | No | **Yes** |
| All-in-one | Partial | No | No | **Yes** |
