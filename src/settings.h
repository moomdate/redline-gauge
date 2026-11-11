#pragma once
// User settings, editable on the device's settings screen (and over serial),
// stored in NVS by main.cpp. Defaults come from config.h.
#include <stdint.h>
#include "config.h"

enum SourceId : uint8_t { SRC_SIM_AUTO, SRC_SIM_TOUCH, SRC_SERIAL, SRC_OBD, SRC_CUSTOM, SRC_HONDA, SRC_COUNT };

// Right-hand panels. AUTO = HYBRID as soon as the source delivers hybrid battery data
// (e.g. OBD on a Civic e:HEV), otherwise STANDARD.
enum PanelSet : uint8_t { PANELS_AUTO, PANELS_STANDARD, PANELS_HYBRID, PANELS_COUNT };
// Gear box: AUTO = estimate from rpm/speed (or use gear= when a source sends it);
// OFF = hidden, for hybrids / CVTs where a gear number means nothing.
enum GearMode : uint8_t { GEARMODE_AUTO, GEARMODE_OFF, GEARMODE_COUNT };
// Honda model profile (HONDA K source): sets the temperature label and default shift point.
//   Wave 110i = air-cooled (OIL TEMP), shift 8500.  Giorno+/Lead = liquid-cooled (COOLANT), shift 9000.
enum HondaModel : uint8_t { HM_WAVE110, HM_GIORNO, HM_COUNT };
// Display skin (whole gauge layout). DEFAULT = the original; DATA WAVE = scrolling-waveform panels;
// RACE CODE = round rev dial with a lit arc.
enum Skin : uint8_t { SKIN_DEFAULT, SKIN_DATA_WAVE, SKIN_RACE_CODE, SKIN_COUNT };
// What each right-hand panel shows; tap a panel to step to the next value that has data.
enum PanelItem : uint8_t {
    PI_COOLANT, PI_VOLTAGE, PI_INTAKE, PI_AFR, PI_THROTTLE,
    PI_FUEL_LEVEL, PI_FUEL_RATE, PI_STFT, PI_LTFT, PI_MAP, PI_IGNITION, PI_O2_MV, PI_LOAD, PI_COUNT
};

struct Settings {
    uint8_t  theme      = 0;             // index into kThemes
    uint8_t  source     = SRC_SIM_AUTO;
    uint16_t shiftRpm   = RPM_SHIFT;     // shift light point
    uint8_t  brightness = 100;           // backlight %, 20..100
    bool     beep       = SHIFT_BEEP;    // shift / warning beeps
    uint8_t  panels     = PANELS_AUTO;
    uint8_t  gearMode   = GEARMODE_AUTO;
    uint8_t  panelItem[3] = { PI_COOLANT, PI_VOLTAGE, PI_INTAKE };
    uint8_t  klineUart  = 2;             // HONDA K pins: 2 = UART2 (22/27), 0 = UART0 (3/1)
    uint8_t  units      = 0;             // speed display: 0 = km/h, 1 = mph (data stays km/h)
    uint8_t  lang       = 0;             // UI language: 0 = EN, 1 = TH
    uint8_t  hondaModel = HM_WAVE110;    // HONDA K profile: temp label + default shift point
    uint8_t  skin       = SKIN_DEFAULT;  // display skin (gauge layout)
    bool     alerts     = true;          // critical warnings: on-screen banner + LED + beep
    uint8_t  alertCoolant = COOLANT_CRIT;  // °C, overheat alert point
    uint8_t  alertVoltLo  = 118;           // 0.1 V units (11.8 V), low-battery alert point
    uint8_t  alertIat     = IAT_CRIT;      // °C, hot-intake alert point
    // Panel colour inversion. The build picks the default (esp32dev = inverted panel), but
    // CYD batches differ, so SETUP -> COLORS flips it at runtime and it is saved.
#ifdef TFT_INVERSION_ON
    bool     invert     = true;
#else
    bool     invert     = false;
#endif

    static const uint16_t kShiftMin = 3000, kShiftMax = RPM_MAX, kShiftStep = 250;
    static const uint8_t  kBrightMin = 20, kBrightStep = 20;
    // Alert threshold stepper ranges (on-device SETTINGS -> ALERT page).
    static const uint8_t  kCoolMin = 95,  kCoolMax = 120, kCoolStep = 1;    // °C
    static const uint8_t  kVLoMin  = 105, kVLoMax  = 125, kVLoStep  = 1;    // 0.1 V (10.5..12.5 V)
    static const uint8_t  kIatMin  = 50,  kIatMax  = 95,  kIatStep  = 5;    // °C
};
