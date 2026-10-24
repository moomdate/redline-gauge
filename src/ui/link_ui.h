#pragma once
// Link page of a source that talks to an ECU (HONDA K, OBD BT): opened by tapping the status
// pill on the gauge. Whether the ECU answers, why not and what to check, the next retry, the
// numbers behind it, and the source's own details (echo / O2 for K-line, Bluetooth for OBD).
#include <stdint.h>
#include "data/link_status.h"
#include "settings.h"

namespace link_ui {

enum Action : uint8_t { ACT_NONE, ACT_BACK };

void   invalidate();                                                    // next render() repaints
void   render(const LinkStatus &st, uint32_t now, const Settings &s);   // repaints on change only
Action tap(int x, int y);

}
