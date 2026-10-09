#pragma once
// Touch calibration maths, no hardware (unit-tested on the host).
// TFT_Touch maps its calibration range onto [30, res-30] px, so the calibration screen puts its 4
// crosses exactly there (30 px in from each edge): the averaged raw readings at the crosses ARE the
// setCal() numbers. Points: 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right.
// a = XPT2046 channel 0x90 (TFT_Touch RawX with axis 1), b = channel 0xD0.
#include <stdint.h>
#include <stdlib.h>

struct TouchCal {
    int16_t xmin, xmax, ymin, ymax;   // a reversed range (min > max) = that axis is mirrored
    uint8_t axis;                     // 1 = channel 0x90 is the screen's X
};

static const int TOUCH_CAL_MARGIN = 30;
static const int TOUCH_CAL_MIN_SPAN = 1000;   // raw units; less = taps missed the crosses

// false when the taps don't form a usable calibration (too small a span on either axis).
inline bool touchCalSolve(const long a[4], const long b[4], TouchCal &out) {
    // the channel that changes more from left to right than from top to bottom is the X axis
    long aAlongX = labs((a[1] + a[3]) - (a[0] + a[2]));
    long aAlongY = labs((a[2] + a[3]) - (a[0] + a[1]));
    bool axis = aAlongX >= aAlongY;
    const long *xs = axis ? a : b, *ys = axis ? b : a;
    out.xmin = (int16_t)((xs[0] + xs[2]) / 2);
    out.xmax = (int16_t)((xs[1] + xs[3]) / 2);
    out.ymin = (int16_t)((ys[0] + ys[1]) / 2);
    out.ymax = (int16_t)((ys[2] + ys[3]) / 2);
    out.axis = axis;
    return abs(out.xmax - out.xmin) >= TOUCH_CAL_MIN_SPAN && abs(out.ymax - out.ymin) >= TOUCH_CAL_MIN_SPAN;
}

// Where TFT_Touch puts a raw reading with this calibration (same formula as the library).
inline int touchCalMap(long raw, int lo, int hi, int res) {
    long v = (raw - lo) * (long)((res - TOUCH_CAL_MARGIN) - TOUCH_CAL_MARGIN) / (hi - lo) + TOUCH_CAL_MARGIN;
    return v < 0 ? 0 : v > res ? res : (int)v;
}
