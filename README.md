# REDLINE

**A motorsport-style digital dash for the $10 "Cheap Yellow Display" (ESP32-2432S028R).**
RPM shift bar, speed, gear, coolant / voltage / intake panels, shift light, warnings —
three colour themes, a touch settings page, a physics simulator, and real data from
OBD-II Bluetooth, USB serial, or your own sensors.

[![CI](https://github.com/moomdate/redline-gauge/actions/workflows/ci.yml/badge.svg)](https://github.com/moomdate/redline-gauge/actions/workflows/ci.yml)
![license](https://img.shields.io/badge/license-PolyForm%20Noncommercial-orange)
![platform](https://img.shields.io/badge/ESP32-CYD%202.8%22-blue)

<p align="center">
  <a href="docs/demo.mp4"><img src="docs/demo.gif" width="240" alt="REDLINE running on a CYD: touch throttle, shift light and the settings page"></a><br>
  <sub>Running on a real CYD, <a href="docs/demo.mp4">full demo video (51 s, MP4)</a></sub>
</p>

> 🇹🇭 คู่มือภาษาไทย (how each mode works, motorcycles/Honda Wave, wiring): **[README.th.md](README.th.md)**

**Features**
- Flicker-free partial redraw at 60 fps, anti-aliased fonts blended onto the artwork
- 3 themes (ICE BLUE, ACID LIME, AMBER), switchable on the device
- Shift light (bar flash, RGB LED, beep), warn/critical colours, stale-data `--`, peak hold, gear estimate
- Data sources: SIM, TOUCH (you're the throttle), SERIAL, OBD BT (ELM327), CUSTOM (your sensors)
- Settings saved in NVS · host-side unit tests · CI builds both panel variants

| ICE BLUE | ACID LIME | AMBER |
|---|---|---|
| ![](docs/theme_ice.png) | ![](docs/theme_lime.png) | ![](docs/theme_amber.png) |

| Boot splash | Settings | Shift light + warnings |
|---|---|---|
| ![](docs/splash.png) | ![](docs/settings_lime.png) | ![](docs/shift.png) |

`docs/sim.gif` is a real render from the firmware code (SIM AUTO mode), produced by the host preview.

## Download (no compiling)

Ready-to-flash `.bin` files for every version are on the **[Releases page](https://github.com/moomdate/redline-gauge/releases)**,
one per panel type (`invert` / `noinvert` / `invert-dim`). Easiest: **REDLINE Flasher**
(`REDLINE-Flasher-windows.exe`, also macOS / Linux) on the same page: it finds the board, downloads the
version you pick and flashes it. Or flash from the browser with [esptool-js](https://espressif.github.io/esptool-js/).
See [which file and how](docs/release-flashing.md).
What changed in each version: [CHANGELOG.md](CHANGELOG.md).

## Build & flash

```bash
ls /dev/cu.*                                                   # find the port (the number changes between plugs)
~/.platformio/penv/bin/pio run -e esp32dev -t upload --upload-port /dev/cu.usbserial-XXXX
~/.platformio/penv/bin/pio device monitor                       # 115200
```
- `esp32dev` = a panel that needs color inversion (the original board)
- `cyd-noinvert` = a panel with normal colors (if colors look inverted, switch env)
- **Touch off / missing taps?** Press the board's **BOOT** button once while REDLINE runs: tap the 4 red crosses,
  check the dots land under your finger, then **SAVE** (kept in flash; `touchcal=reset` undoes it, serial `touchcal`
  opens it too). Build-time defaults: `-D TOUCH_CAL_XMIN/XMAX/YMIN/YMAX/AXIS` in `platformio.ini`.
  No reaction to taps even there: the board isn't an ESP32-2432S028**R** (2.4"/3.2", capacitive or no-touch).

## Using it

| Action | Result |
|---|---|
| **Tap SETUP** (bottom-right) or the status bar | Open the settings page |
| **Hold the main area** in SIM TOUCH | Throttle: further right = more throttle, release to coast |
| **Long-press the main area** (other modes) | Reset PEAK RPM |

### Boot splash
The REDLINE wordmark appears on your saved theme, and its mini shift bar fills as the board starts.
Credit: *crafted by birdlab.th*. The splash lasts 1.8 s; you can tap to skip it after 1 s.
The credit is a **Required Notice** of the license (see [NOTICE](NOTICE)), so it can't be turned off.
If the splash is removed or the logo is changed, the status bar shows `UNOFFICIAL` instead of REDLINE.

### Settings page
- **THEME**: ICE BLUE / ACID LIME / AMBER (tap a thumbnail to change the theme immediately)
- **DATA SOURCE**: SIM · TOUCH · SERIAL · OBD BT · CUSTOM
- **SHIFT LIGHT RPM**: ± 250 (3,000 – `RPM_MAX`)
- **BRIGHTNESS**: on the `-dim` images (`invert-dim` / `noinvert-dim`, envs `esp32dev-dim` / `cyd-noinvert-dim`, or `BACKLIGHT_DIMMING 1` in `config.h`): 20–100 %. The plain images keep the backlight full-on.
- **BEEP ON/OFF**, **RESET PEAK**, **DONE** to go back
- **PANEL AUTO / STD / HYB**: right-hand panels. STD = COOLANT · VOLTAGE · INTAKE; HYB = COOLANT · HV BATT (%) · HV POWER (kW, negative = regen);
  AUTO switches to HYB as soon as the source delivers hybrid battery data (e.g. OBD on a Honda e:HEV)
- **COLORS**: flips the panel colours (inverted / normal) and saves it, for when the screen shows a white background after flashing.
- **GEAR AUTO / OFF**: OFF hides the gear box — use it on hybrids and CVTs, where an estimated gear means nothing

Every value is saved in NVS and restored after a reboot.

Serial commands (115200): `mode=sim|touch|serial|obd|custom` `theme=ice|lime|amber` `shift=6500`
`beep=on|off` `peak=reset` `panels=auto|standard|hybrid` `gearmode=auto|off` `timer` `timerlog` `timerlog=clear` `obd=scan` `obd=AA:BB:CC:DD:EE:FF` `obdpin=1234` `bench` (timing test) `help` — and `bright=80` when `BACKLIGHT_DIMMING` is 1

### Status bar
Dot color: 🟢 live data · 🔵 simulator · 🟡 (blinking) connecting · 🔴 error.
The right side shows the session time, or a status message such as `BT SCAN`, `BT PAIRING`, `ELM INIT`, `ECU SEARCH`, `NO ADAPTER`, `NO ECU`, `WAIT DATA`.

### Warnings
- RPM ≥ `RPM_SHIFT` → the bar flashes red, the RPM number turns red, the RGB LED strobes red, and there is a short beep.
- Coolant / Voltage / Intake past their thresholds → value turns yellow (warn) or blinks red (critical), the LED blinks amber, and there is one beep.
- Coolant below `COOLANT_COLD` → shown blue (engine not warm yet).
- Any value not updated for 2.5 s → shows `--` (data never freezes on screen).

## Data sources

![Where REDLINE gets its data](docs/wiring/system-overview.svg)

### 1. SIM AUTO / SIM TOUCH (simulator)
A physics-based car model: torque curve, 6-speed gearbox with auto-shift/kick-down, clutch launch, drag, braking,
coolant/intake heat model, and alternator voltage. It even cranks on start-up (voltage dips to ~10.6 V).
SIM AUTO loops a scripted drive: idle → city → full-throttle pull through the shift light → braking → highway.

### 2. OBD BT — ELM327 Bluetooth Classic (real car)

**Which cars work?** See the [compatibility checklist](docs/compatibility.md): Thai-market pickups, sedans, hybrids and EVs.

1. Plug the ELM327 into the car's OBD port and turn the ignition to ON / start the engine.
   **Disconnect any phone from the adapter first** (close the OBD app / turn phone Bluetooth off):
   an ELM327 accepts only one connection at a time.
2. Select OBD BT mode. It goes BT SCAN → BT PAIRING → ELM INIT → ECU SEARCH → green dot.
   - The scan (~10 s) lists every Bluetooth Classic device on the USB serial log and picks the one whose
     name looks like an OBD adapter (`OBDII`, `OBD2`, `V-LINK`, `VEEPEAK`, `KONNWEI`, `Vgate`…).
   - Its MAC is saved, so later boots connect straight away without scanning.
   - The PIN is tried as 1234 → 0000 → 6789 → 1111 automatically.
3. Still `NO ADAPTER`? Open `pio device monitor`, read the scan list, then type:
   - `obd=AA:BB:CC:DD:EE:FF` to pick an adapter with an unusual name,
   - `obdpin=xxxx` to fix the PIN,
   - `obd=scan` to forget the saved adapter.
   An empty scan list usually means the adapter is BLE-only (see below) or a phone is still connected to it.

Reads: RPM (010C), speed (010D), coolant (0105), intake (010F), battery voltage (ATRV), and on hybrids
HV battery SOC (015B) and HV battery power from voltage × current (019A). Only PIDs the car lists in its
supported-PID bitmasks (0100 … 0180) are polled.

**Hybrids (e.g. Honda Civic e:HEV)**: set GEAR to OFF (there is no stepped gearbox). PANEL AUTO turns on
HV BATT / HV POWER once the car answers 015B / 019A. In EV mode the engine is off, so RPM reads 0. If HV POWER
reads negative while accelerating, set `HV_CURRENT_SIGN -1` in `config.h`. Intake temp (010F) is not reported by
the Civic e:HEV.
RPM is polled every other request so the bar stays responsive. PIDs the car doesn't support are skipped automatically.
Gear is estimated from rpm/speed → set `GEAR_RATIOS`, `FINAL_DRIVE`, `TIRE_CIRCUMFERENCE_M` for your car
(or `GEAR_COUNT 0` to hide it).
> BLE-only adapters (e.g. Vgate iCar Pro BLE) don't work with this path — you need a Bluetooth Classic (SPP) one.

### 3. SERIAL — send values from another device
One line per update, any subset of values:
```
rpm=3200 spd=86 clt=87 volt=13.9 iat=42 gear=3
{"rpm":3200,"speed":86,"coolant":87,"voltage":13.9,"iat":42}
```
Test from a Mac: `~/.platformio/penv/bin/python tools/serial_feed.py /dev/cu.usbserial-XXXX`
(it sends `mode=serial` and a sweep of values automatically).

**From an Arduino / another board:** wire its TX → CYD **GPIO 27** (CN1) + GND. That's a
receive-only UART, so USB stays free. Ready-made sketches with wiring are in **[examples/](examples/README.md)**:
`SerialSenderDemo` (test values) and `SerialSenderSensors` (RPM pulse, speed, battery, NTC temps).

![Arduino Uno / Nano to CYD wiring](docs/wiring/wiring-arduino-serial.svg)

### 4. CUSTOM — your own values (sensors wired straight to the ESP32, CAN, etc.) ← easiest
Edit only **`src/my_sensors.cpp`**: replace its two functions with your own, flash, then select SETUP → DATA SOURCE → CUSTOM.
A complete example (battery + RPM, one sensor per pin):
```cpp
#include "gauge_input.h"

static PulseInput tach;                              // declared once, outside the functions

void mySensorsBegin() {
    analogSetPinAttenuation(35, ADC_11db);           // GPIO 35: full 0..3.1 V range
    tach.begin(22, 60.0f / 2);                       // GPIO 22, 2 pulses per rev (4-cyl)
}

void mySensorsRead(GaugeInput &in) {
    in.voltage = readDividerVolts(35, 47000, 10000); // battery via 47k/10k divider on GPIO 35
    in.rpm     = tach.value();                       // ignition pulse via opto on GPIO 22
    // A second analog sensor needs its own pin, e.g. an NTC on GPIO 27
    // (set EXT_SERIAL_RX_PIN -1 in config.h first):
    // in.coolant = readNtcCelsius(27, 2200, 2500, 3950);
}
```
- Set only the fields you have. Anything left as `NAN` shows `--`.
- Ready-made helpers in `src/gauge_input.h`: `readDividerVolts`, `readNtcCelsius`, `PulseInput` (RPM/VSS from pulses).
- Try it without wiring: set `EXAMPLE_FAKE_VALUES 1` in `my_sensors.cpp`.
- Values coming from elsewhere (a CAN callback, a BLE notify): call `gauge::set(CH_RPM, v)` from any task. It is thread-safe.
- Free CYD pins: GPIO 35 (P3, analog/input only), GPIO 22 (CN1). GPIO 27 too if you set `EXT_SERIAL_RX_PIN -1`. Car signals are 12–14 V: always use a divider or opto-isolator.

For a completely new source type, subclass `DataSource` (`src/data/source.h`) and add it to `sources[]` in `main.cpp`.

#### Wiring diagrams

Check the labels printed next to each connector first — the pin order differs between CYD batches.

![CYD connectors](docs/wiring/cyd-connectors.svg)

| Battery voltage → GPIO 35 | Temperature sender (NTC) |
|---|---|
| ![Battery divider](docs/wiring/wiring-battery.svg) | ![NTC](docs/wiring/wiring-ntc.svg) |

![RPM pulse via PC817](docs/wiring/wiring-rpm-opto.svg)

## Drag timer (0-100, 100-120, 120-160, 0-200)

SETUP → **TIMER** (or `timer` over serial). Works with any source that delivers speed (OBD, SERIAL / GPS, CUSTOM, SIM).

| Ready | Running | Finish | Run log |
|---|---|---|---|
| ![timer ready](docs/timer-ready.png) | ![timer running](docs/timer-run.png) | ![timer finish](docs/timer-finish.png) | ![run log](docs/timer-log.png) |

- Open it with the car stopped and it is armed at once: a big flashing **READY!!** and three amber lights.
  (Rolling: **STOP TO ARM**.) The boxes keep showing your last run.
- The clock (**TIME**, bottom row) starts the moment the speed leaves 0, back-estimated from the first
  samples, and the big number is the live speed.
- Reach 100 km/h and the **0-100** box is stamped with its time (a beep, the box flashes amber); 120 stamps
  **100-120**, 160 stamps **120-160**, 200 stamps **0-200** and finishes the run. The box being chased is
  highlighted with a bar that fills with speed. Faster than your log's best = green **BEST** / **NEW BEST**.
- A run that never gets to 200 ends when the speed drops 10 km/h under its maximum (lift / brake) or stops
  climbing for 3 s. Every run past 30 km/h is **SAVED** with whatever it reached: top speed, the time to it
  (e.g. "0-144 10.30 SEC") and every split it passed. Stop and it re-arms for the next run.
- **LOG**: last 20 runs (0-100, 100-120, 120-160, 0-200, top speed @ time to it; 160-200 is kept too), stored on
  the board. **CLEAR LOG** asks **CONFIRM CLEAR** / **CANCEL**. Over serial: `timerlog` prints CSV, `timerlog=clear`;
  the timer also logs READY / START / every stamp / saved runs as `[timer]` lines.
- SIM AUTO: opening the timer puts the car at the line; it launches by itself, runs to 200, brakes back to the
  line and goes again.
- Accuracy: OBD speed is whole km/h at a few Hz (the timer screen polls speed every other request), so expect
  about ±0.2 s. A 10 Hz GPS feeding `spd=` over SERIAL is better. For fun and comparing your own runs —
  do it on a track, not on public roads.

## Honda motorcycles over K-line (HONDA K, experimental)

SETUP → DATA SOURCE → **HONDA**, or `mode=honda`. This reads the ECU of the **Honda Wave 110i / 125i**
through its red 4-pin diagnostic connector. Other models may lay out the data differently.

- **Hardware**: a K-line transceiver board between the bike and the CYD, either an L9637D board or an
  opto-isolated "K-line FTDI" board.
  - Car side: orange = K-line, green = GND, white/black = +12 V switched. Leave brown (SCS) unconnected.
  - CYD side (CN1): board RX-out → **GPIO 22**, **GPIO 27** → board TX-in, VCC → **3.3 V** (never 5 V), GND.
    Pins: `KLINE_RX_PIN` / `KLINE_TX_PIN` in `config.h`.
- **What it does**: wakes the ECU (K low 70 ms, high 130 ms), sends `FE 04 72 8C` and `72 05 00 F0 99`,
  then polls table 0x17 (`72 05 71 17 01`).
- **Values**: RPM, throttle %, engine temp, battery. No speed or gear: the Wave's speedo is mechanical.
- **Status bar**:
  - `K-LINE INIT` while connecting.
  - `NO K-LINE` when no echo came back: wiring or TX/RX swapped.
  - `NO ECU` when the wiring echoes but the ECU doesn't answer: ignition off, or a CAN bike.
- **HONDA TEST** (SETUP → DATA SOURCE → **H TEST**, or `mode=hondatest`) helps find the bytes on a new
  bike without a laptop. Same link and values, but every 5 s the **SPEED** slot shows a different raw byte of
  table 0x17, named in the status bar: `A d[4]`, `B d[5]`, `C d[6]` … (one minute per round).
  Ride or spin the wheel and note which letter follows the speedo. **COOLANT** shows d[5] − 40 next to
  **INTAKE** d[7] − 40: whichever climbs to ~90 °C after a warm-up is the engine temperature.
- **`kdump`** over serial prints every table raw once a second and marks the bytes that changed. Use it to
  check the byte layout on a new model: idle, then blip the throttle.
- Experimental: it has not been run on a bike with REDLINE yet. Please report results.

## Motorcycles (e.g. Honda Wave)
OBD BT does **not** work on Honda bikes: their 4-pin DLC speaks Honda's own K-Line protocol, not car OBD-II.
Use **CUSTOM** instead: ignition pulse → opto → GPIO 22 (`PulseInput`, usually 1 pulse/rev on a single),
battery divider on GPIO 35, oil-temp NTC on `in.coolant` with `PANEL1_LABEL "OIL TEMP"`, and
`RPM_MAX 10000`, `GEAR_COUNT 0` in `config.h`. Details in README.th.md §4.

## Tests

```bash
~/.platformio/penv/bin/pio test -e native    # 29 host tests: parsers, model, renderer, settings, simulator, theme assets
```
CI (GitHub Actions) runs the tests and builds both firmware variants on every push.

## Design sources
- `tools/art/bg_*.png`: the original 320×240 artwork of the 3 themes. `gen_bg.py` turns them into `src/assets/bg_*.h`

## Structure

```
src/
  main.cpp              setup/loop, data task (core 0), touch, LED + buzzer
  config.h              ← everything you tune: RPM_MAX, RPM_SHIFT, thresholds, gear ratios, OBD
  my_sensors.cpp        ← your own sensor code (CUSTOM source)
  gauge_input.h         GaugeInput + helpers (divider, NTC, pulse)
  settings.h            Settings struct (saved in NVS)
  data/gauge_bus.*      thread-safe store of the latest values (every source writes here)
  data/sim_source.*     simulator
  data/obd_source.*     ELM327 Bluetooth state machine
  data/serial_source.*  serial parser
  ui/gauge_model.*      raw values → what to show (smoothing, peak, warning levels, gear, status)
  ui/gauge_ui.*         layout + partial redraw per region + SETUP button
  ui/settings_ui.*      settings page
  ui/theme.*            theme table (background + accent colors)
  ui/canvas.*           RGB565 renderer + AA text blended onto the background image
  ui/shift_slots.h      13-slot shift-bar geometry pulled from the background (generated)
  fonts/*.h             Barlow Condensed / IBM Plex Mono (generated)
  assets/bg_*.h         3 theme backgrounds, 320×240 RGB565 (in flash, 150 KB each)
tools/
  host_preview/run.sh   build the real UI + simulator on the Mac → PNG/GIF (no board needed)
  gen_font.py, make_fonts.sh, gen_slots.py, gen_bg.py (--vivid), serial_feed.py
```

### Rendering design
- The background (150 KB) stays in flash. The screen is split into regions (status, shift bar, RPM label, RPM number,
  speed, 3 panels, SETUP). A region is redrawn only when its content changes: compose background + text into a 28 KB
  buffer, then push once → no flicker.
- Inside a region only what moved is pushed: the shift bar sends just the columns between the old and new lit edge,
  and the RPM / speed numbers (tabular digits) send just the digit cells that changed. The whole region is still
  painted and clipped, so the result is pixel-identical to a full repaint — a host test checks that on every frame
  of a full sweep.
- SPI runs at 80 MHz (all images). Beeps are kept at 60 % volume, and touch clicks never overlap a redraw: a full-volume beep during a big push could garble the picture on PWM-dimmed boards.
- Measured on the CYD with `bench` (serial command, fixed 0 → 8000 → 0 sweep): **7.9 ms per frame, 126 fps possible** at 80 MHz SPI (8.7 ms / 115 fps at 40 MHz)
  (was 23.6 ms / 42 fps), 11.7 KB pushed per frame (was 44 KB). The UI runs capped at 60 fps (`UI_FPS`).
- Fonts are anti-aliased and blended against the *actual background pixels*
  (TFT_eSPI smooth fonts blend against a single color, which leaves edge halos on an image background).
- Shift-bar segments sit exactly on the 13 slots painted in the art, with partial fill at the leading edge
  plus a peak-hold marker that falls back.

### Checking the UI without a board
```bash
PY=/path/to/python-with-pillow tools/host_preview/run.sh      # → tools/host_preview/out/*.png, sim.gif
```

### Regenerating fonts / changing the background
```bash
PY=/path/to/python-with-pillow tools/make_fonts.sh
python3 tools/gen_bg.py <png> bg_name src/assets/bg_name.h   # add a theme → add a row in src/ui/theme.cpp
```
Fonts: Barlow Condensed and IBM Plex Mono — SIL Open Font License (`tools/font_src/OFL.txt`).

## License

**PolyForm Noncommercial 1.0.0**: see [LICENSE.md](LICENSE.md) and [NOTICE](NOTICE)
(Thai summary with examples: [LICENSE.th.md](LICENSE.th.md)).
You may use, modify and share REDLINE for free for any **noncommercial** purpose
(personal projects, your own car, learning, research, hobby clubs).
**Selling it or using it in a commercial product or service is not allowed.**
For commercial licensing, open an issue to get in touch.

> This is a *source-available* license, not an OSI "open source" license, because it restricts commercial use.

The fonts (Barlow Condensed, IBM Plex Mono) are © their authors under the SIL Open Font License 1.1.
