#pragma once
#define REDLINE_VERSION "1.6.6"
// ============================================================================
//  Smart gauge configuration — everything you'd tune for a specific car lives here.
// ============================================================================

// ---- RPM bar / shift light -------------------------------------------------
#define RPM_MAX            8000    // full scale of the 13-slot shift bar
#define RPM_SHIFT          7000    // shift light: bar flashes, LED red, beep
#define RPM_IDLE            850    // simulator idle
#define RPM_DISPLAY_STEP     50    // big RPM number is rounded to this (reduces flicker)

// ---- Warning thresholds (value turns yellow = warn, red + blink = critical) --
#define COOLANT_COLD         60    // below: shown blue (engine not warm yet)
#define COOLANT_WARN        100
#define COOLANT_CRIT        105
#define VOLT_LOW_CRIT      11.8f
#define VOLT_LOW_WARN      12.4f
#define VOLT_HIGH_WARN     14.8f
#define VOLT_HIGH_CRIT     15.2f
#define IAT_WARN             55
#define IAT_CRIT             70

// Ranges of the thin bar graphs under each side-panel value.
#define COOLANT_BAR_MIN      40
#define COOLANT_BAR_MAX     120
#define VOLT_BAR_MIN       10.0f
#define VOLT_BAR_MAX       16.0f
#define IAT_BAR_MIN         -10
#define IAT_BAR_MAX          80

// ---- Hybrid panels (SETUP -> PANELS: HYBRID, or AUTO when the car reports them) ----
#define HYBRID_PANEL2_LABEL "HV BATT"   // state of charge, %
#define HYBRID_PANEL3_LABEL "HV POWER"  // battery power, kW (negative = regen)
#define SOC_LOW_WARN         15
#define SOC_LOW_CRIT          8
#define HVKW_BAR_MAX         60     // |kW| that fills the bar
// OBD PID 9A battery current sign. SAE says positive = discharge; flip to -1 if HV POWER
// reads negative while accelerating on your car.
#define HV_CURRENT_SIGN       1

// ---- Data freshness ----------------------------------------------------------
#define DATA_STALE_MS      2500    // a value not refreshed for this long shows "--"
#define PEAK_HOLD_MS       1500    // peak marker on the shift bar holds, then falls

// ---- Gear estimation (used when the source doesn't report a gear, e.g. OBD) --
// gear = the ratio closest to  engine_rpm / wheel_rpm / FINAL_DRIVE.
// Defaults are a typical 6-speed hatchback. GEAR_COUNT 0 = no estimate and the
// simulator shows no gear; a source that sends gear= explicitly still shows it.
#define GEAR_COUNT           6
#define GEAR_RATIOS        { 3.636f, 2.235f, 1.521f, 1.137f, 0.891f, 0.707f }
#define FINAL_DRIVE        4.06f
#define TIRE_CIRCUMFERENCE_M 1.94f  // e.g. 205/55R16 ≈ 1.94 m
#define GEAR_TOLERANCE     0.12f    // ±12 % of a ratio still counts as that gear

// ---- OBD-II over Bluetooth Classic (ELM327 "OBDII" dongles) -----------------
// BLE-only adapters (Vgate iCar Pro BLE, some Veepeak) are NOT supported by this path.
#define OBD_BT_NAME        "OBDII"  // preferred name; any "OBD/ELM/V-LINK/…" device found by the scan also works
#define OBD_BT_MAC         ""       // optional fixed MAC (or type obd=AA:BB:… on serial; the found one is saved)
#define OBD_BT_PIN         "1234"   // tried first, then 1234 / 0000 / 6789 / 1111 (obdpin=… fixes one)
#define OBD_CMD_TIMEOUT_MS 1500

// ---- Serial input (USB) ------------------------------------------------------
#define SERIAL_BAUD      115200

// Second, receive-only serial input for an external board (Arduino, ESP32, Pi …)
// so USB stays free for flashing/logs. Wire: sender TX -> CYD GPIO 27 (CN1), GND -> GND.
// 5 V boards (Uno/Nano/Mega) need a divider on TX: 1k to TX, 2k to GND, middle to GPIO 27.
// Same line protocol as USB (see serial_source.h). Set -1 to free GPIO 27 for other uses.
#define EXT_SERIAL_RX_PIN   27
#define EXT_SERIAL_BAUD  115200

// ---- Honda K-line (HONDA K source: Wave 110i / 125i, red 4-pin connector) ----------------
// Through a K-line transceiver board (L9637D / MC33660 / opto, logic VCC from 3.3 V):
// board RX-out -> KLINE_RX_PIN, KLINE_TX_PIN -> board TX-in. Both on CN1.
// GPIO 27 is also the external serial input; HONDA K takes it over while it runs.
#define KLINE_RX_PIN        22
#define KLINE_TX_PIN        27
#define KLINE_INVERT         0      // 1 = inverting opto interface (DIY 2-opto schematic); also `klineinvert=on`

// ---- Hardware (CYD) -----------------------------------------------------------
#define PIN_SPEAKER          26
#define PIN_LED_R             4     // RGB LED, active LOW
#define PIN_LED_G            16
#define PIN_LED_B            17
#define SHIFT_BEEP            1     // 1 = short beep when crossing RPM_SHIFT
// Speaker loudness, % of full (square-wave duty: 100 % = 50 % duty). Kept at 60 %: full-volume
// beeps pull current spikes that, with a PWM-dimmed backlight, could garble the 80 MHz display.
#define SPEAKER_VOLUME       60
#define TOUCH_LONGPRESS_MS  800
// Touch calibration: touch.setCal(XMIN, XMAX, YMIN, YMAX, 320, 240, AXIS). Defaults = the reference
// board. Each board can calibrate itself (BOOT button -> 4 crosses -> SAVE, kept in NVS); these are
// only the starting values. Override per build in platformio.ini (-D TOUCH_CAL_XMIN=... etc.).
#ifndef TOUCH_CAL_XMIN
#define TOUCH_CAL_XMIN      526
#endif
#ifndef TOUCH_CAL_XMAX
#define TOUCH_CAL_XMAX     3443
#endif
#ifndef TOUCH_CAL_YMIN
#define TOUCH_CAL_YMIN      750
#endif
#ifndef TOUCH_CAL_YMAX
#define TOUCH_CAL_YMAX     3377
#endif
#ifndef TOUCH_CAL_AXIS
#define TOUCH_CAL_AXIS        1     // 1 = XPT2046 channel 0x90 is the screen's X
#endif

// ---- Side-panel captions (uppercase A-Z and spaces only) ----------------------
// Air-cooled motorcycle (e.g. Honda Wave)? publish oil temp on CH_COOLANT and
// rename the panel "OIL TEMP".
#define PANEL1_LABEL  "COOLANT"
#define PANEL2_LABEL  "VOLTAGE"
#define PANEL3_LABEL  "INTAKE"

// ---- UI ------------------------------------------------------------------------
#define UI_FPS               60     // partial redraws keep a frame well under 16 ms (see `bench`)
#define STATUS_TITLE       "REDLINE"

// ---- Backlight ---------------------------------------------------------------------------
// 1 = PWM-dim the backlight (BRIGHTNESS 20-100 % on the SETUP page); the *-dim envs set it.
// Default 0 = driven plain HIGH, BRIGHTNESS hidden. (Up to 1.5.2 a "goes dark when dimmed"
// bug was blamed on the hardware; it was a lost LEDC duty write, fixed in 1.5.3.)
#ifndef BACKLIGHT_DIMMING          // (#ifndef so a board env can turn it on with -D)
#define BACKLIGHT_DIMMING 0
#endif
