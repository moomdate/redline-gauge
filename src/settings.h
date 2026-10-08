#pragma once
// User settings, editable on the device's settings screen (and over serial),
// stored in NVS by main.cpp. Defaults come from config.h.
#include <stdint.h>
#include "config.h"

enum SourceId : uint8_t { SRC_SIM_AUTO, SRC_SIM_TOUCH, SRC_SERIAL, SRC_OBD, SRC_CUSTOM, SRC_HONDA, SRC_HONDA_TEST, SRC_COUNT };

// Right-hand panels. AUTO = HYBRID as soon as the source delivers hybrid battery data
// (e.g. OBD on a Civic e:HEV), otherwise STANDARD.
enum PanelSet : uint8_t { PANELS_AUTO, PANELS_STANDARD, PANELS_HYBRID, PANELS_COUNT };
// Gear box: AUTO = estimate from rpm/speed (or use gear= when a source sends it);
// OFF = hidden, for hybrids / CVTs where a gear number means nothing.
enum GearMode : uint8_t { GEARMODE_AUTO, GEARMODE_OFF, GEARMODE_COUNT };

struct Settings {
    uint8_t  theme      = 0;             // index into kThemes
    uint8_t  source     = SRC_SIM_AUTO;
    uint16_t shiftRpm   = RPM_SHIFT;     // shift light point
    uint8_t  brightness = 100;           // backlight %, 20..100
    bool     beep       = SHIFT_BEEP;    // shift / warning beeps
    uint8_t  panels     = PANELS_AUTO;
    uint8_t  gearMode   = GEARMODE_AUTO;
    // Panel colour inversion. The build picks the default (esp32dev = inverted panel), but
    // CYD batches differ, so SETUP -> COLORS flips it at runtime and it is saved.
#ifdef TFT_INVERSION_ON
    bool     invert     = true;
#else
    bool     invert     = false;
#endif

    static const uint16_t kShiftMin = 3000, kShiftMax = RPM_MAX, kShiftStep = 250;
    static const uint8_t  kBrightMin = 20, kBrightStep = 20;
};
