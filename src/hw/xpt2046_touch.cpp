#include "hw/xpt2046_touch.h"
#include <Arduino.h>
#include "ui/touch_cal_math.h"

static const uint8_t CMD_X = 0x91, CMD_Y = 0xD1, CMD_Z1 = 0xB1, CMD_IDLE = 0xD0;   // 0xD0: power down, PENIRQ on

Xpt2046Touch::Xpt2046Touch(int cs, int clk, int din, int dout) : cs_(cs), clk_(clk), din_(din), dout_(dout) {}

void Xpt2046Touch::begin() {
    pinMode(cs_, OUTPUT);  digitalWrite(cs_, HIGH);
    pinMode(clk_, OUTPUT); digitalWrite(clk_, LOW);
    pinMode(din_, OUTPUT); digitalWrite(din_, LOW);
    pinMode(dout_, INPUT);
    read12(CMD_IDLE);
}

// 8 command bits out, then 16 clocks in: 12 data bits (MSB first) and 4 trailing zeros.
uint16_t Xpt2046Touch::read12(uint8_t cmd) {
    digitalWrite(cs_, LOW);
    for (int i = 7; i >= 0; i--) {
        digitalWrite(din_, (cmd >> i) & 1);
        delayMicroseconds(1); digitalWrite(clk_, HIGH); delayMicroseconds(1); digitalWrite(clk_, LOW);
    }
    uint16_t v = 0;
    for (int i = 0; i < 16; i++) {
        delayMicroseconds(1); digitalWrite(clk_, HIGH); delayMicroseconds(1); digitalWrite(clk_, LOW);
        v = (uint16_t)((v << 1) | digitalRead(dout_));
    }
    digitalWrite(cs_, HIGH);
    return v >> 4;
}

uint16_t Xpt2046Touch::median5(uint8_t cmd) {
    uint16_t s[5];
    for (auto &v : s) v = read12(cmd);
    for (int i = 1; i < 5; i++)
        for (int j = i; j > 0 && s[j] < s[j - 1]; j--) { uint16_t t = s[j]; s[j] = s[j - 1]; s[j - 1] = t; }
    return s[2];
}

void Xpt2046Touch::setCal(int xmin, int xmax, int ymin, int ymax, int xres, int yres, bool axis) {
    xmin_ = xmin; xmax_ = xmax; ymin_ = ymin; ymax_ = ymax; xres_ = xres; yres_ = yres; axis_ = axis;
}

bool Xpt2046Touch::Pressed() {
    z_ = read12(CMD_Z1);
    if (z_ < TOUCH_Z_MIN) { read12(CMD_IDLE); return false; }
    uint16_t cx = median5(CMD_X), cy = median5(CMD_Y);
    bool down = read12(CMD_Z1) >= TOUCH_Z_MIN                     // still pressed: not a half-way reading
                && cx > 40 && cx < 4055 && cy > 40 && cy < 4055;   // pinned to a rail: not a real press
    read12(CMD_IDLE);
    if (!down) return false;
    rawX_ = axis_ ? cx : cy;
    rawY_ = axis_ ? cy : cx;
    if (xmax_ != xmin_) x_ = (uint16_t)touchCalMap(rawX_, xmin_, xmax_, xres_);
    if (ymax_ != ymin_) y_ = (uint16_t)touchCalMap(rawY_, ymin_, ymax_, yres_);
    return true;
}
