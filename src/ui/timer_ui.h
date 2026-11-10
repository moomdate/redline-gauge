#pragma once
// Drag timer screen and run log page. Draws on the gauge's themed art with the same
// region layout (status bar, shift bar, big number, speed, three side panels), and
// like the gauge only re-pushes a region when what it shows changed.
#include <stdint.h>
#include "ui/drag_timer.h"

namespace timer_ui {

enum Action : uint8_t { ACT_NONE, ACT_EXIT, ACT_ABORT, ACT_AGAIN, ACT_LOG, ACT_BACK, ACT_CLEAR_ASK, ACT_CLEAR,
                        ACT_CANCEL, ACT_NEW_RUN };

void   invalidate();                                  // full repaint next render()
void   setUnits(uint8_t units);                       // 0 = km/h, 1 = mph (live + top speed)
void   render(const DragTimer &t, const RunLog &log, uint32_t now, bool speedValid);
Action tapTimer(int x, int y, const DragTimer &t);

void   drawLog(const RunLog &log);                    // CLEAR asks CONFIRM / CANCEL first
Action tapLog(int x, int y);
bool   logConfirming();                               // CLEAR tapped, CONFIRM / CANCEL shown

}
