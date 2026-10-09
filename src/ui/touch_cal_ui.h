#pragma once
// On-device touch calibration screen. Opened with the board's BOOT button (works even when touch
// is off or mirrored), or serial `touchcal`. Tap 4 crosses, test by drawing, then SAVE.
// Blocks until done; the caller repaints its screen afterwards.
#include "ui/touch_cal_math.h"

class TFT_eSPI;
class TFT_Touch;

namespace touch_cal_ui {

// cal: in = the calibration in use (restored on cancel), out = the saved one.
// Returns true when the user saved a new calibration.
bool run(TFT_eSPI &tft, TFT_Touch &touch, TouchCal &cal, int bootPin);

}
