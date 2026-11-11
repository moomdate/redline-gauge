#pragma once
// Full-screen settings page: theme (with live thumbnails), data source,
// shift-light RPM, backlight brightness, beep on/off, reset peak.
// Opened from the SETUP button (or by tapping the status bar).
#include "settings.h"

namespace settings_ui {

enum Action : uint8_t {
    ACT_NONE,
    ACT_CHANGED,     // a setting changed — apply it (and persist)
    ACT_RESET_PEAK,
    ACT_CLOSE,       // DONE pressed — back to the gauge
    ACT_TIMER,       // TIMER pressed — open the drag timer
};

// Source names shown on the buttons, indexed by SourceId.
extern const char *const kSourceLabels[SRC_COUNT];
extern const char *const kPanelLabels[PANELS_COUNT];

void   draw(const Settings &s);                 // full repaint (open / after a change)
void   openMain();                              // reset to the main page (call when (re)opening)
Action tap(int x, int y, Settings &s);          // handle a tap; edits s, repaints if needed

}
