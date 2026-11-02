#pragma once
// BLUETOOTH pair modal: opened from the OBD BT link page. Scans for adapters, shows a scrollable
// list, and pairs with the one you tap (PIN is tried automatically — no typing). The scan itself
// runs in ObdSource on the data task; this only draws the list and routes taps.
#include <stdint.h>
#include "settings.h"

namespace scan_ui {

enum Action : uint8_t { ACT_NONE, ACT_CLOSE, ACT_RESCAN, ACT_PAIR };

void        open();                               // reset selection; the caller starts the scan
void        invalidate();                         // force a repaint next render()
void        render(uint32_t now, const Settings &s);
Action      tap(int x, int y);
const char *selectedMac();                        // "" when nothing is selected

}
