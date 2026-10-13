#pragma once
// XPT2046 resistive touch on its own bit-banged pins (ESP32-2432S028R: CS 33, CLK 25, DIN 32, DOUT 39).
// Replaces the TFT_Touch library, keeping its calibration numbers (setCal / RawX / X), but reads
// the chip the way cyd-touch-finder does, which calibrates and taps accurately on our boards:
//   - ~1 us per clock edge: the library's unthrottled bit-bang read garbage on one of our CYDs
//   - a press needs real pressure (Z1 >= TOUCH_Z_MIN, checked again after sampling): a light
//     touch reads pulled toward the centre, so it is ignored instead of landing off target
//   - each axis is the median of 5 conversions, readings pinned to the rails are dropped
//   - no "two reads within 10 raw units" rule: that one made a held finger flicker released
#include <stdint.h>

#ifndef TOUCH_Z_MIN
#define TOUCH_Z_MIN 150        // Z1 pressure for a press (idle reads ~0); lower = lighter touch counts
#endif

class Xpt2046Touch {
public:
    Xpt2046Touch(int cs, int clk, int din, int dout);
    void     begin();
    bool     Pressed();                   // samples the panel; true = pressed, X/Y/RawX/RawY updated
    uint16_t X() const { return x_; }     // screen px with the calibration
    uint16_t Y() const { return y_; }
    uint16_t RawX() const { return rawX_; }   // channel 0x90 when axis = 1 (as TFT_Touch)
    uint16_t RawY() const { return rawY_; }
    uint16_t Z() const { return z_; }
    // Same meaning as TFT_Touch::setCal: raw [xmin, xmax] maps to [30, xres - 30] px.
    void setCal(int xmin, int xmax, int ymin, int ymax, int xres, int yres, bool axis);

private:
    int cs_, clk_, din_, dout_;
    int xmin_ = 0, xmax_ = 4095, ymin_ = 0, ymax_ = 4095, xres_ = 320, yres_ = 240;
    bool axis_ = true;
    uint16_t x_ = 0, y_ = 0, rawX_ = 0, rawY_ = 0, z_ = 0;
    uint16_t read12(uint8_t cmd);
    uint16_t median5(uint8_t cmd);
};
