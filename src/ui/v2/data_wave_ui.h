#pragma once
// DATA WAVE skin: big speed + gear on the left, four scrolling-waveform panels
// (RPM / BOOST / AFR / ECT) on the right. Composes over a flat dark backdrop
// (Canvas::beginSolid) and shares gauge_ui's scratch canvas + push function, so
// only one skin is live at a time and no extra framebuffer RAM is used.
#include "ui/gauge_ui.h"   // PushFn, Theme, GaugeView

namespace data_wave_ui {
void begin(PushFn push, const Theme &theme);
void setTheme(const Theme &theme);   // re-tint + full repaint
void setUnits(uint8_t units);        // 0 = km/h, 1 = mph
void redrawAll();                    // clear to the backdrop, then every region next render()
void invalidate();                   // force every region to redraw next frame
void render(const GaugeView &v);
int  lastPushedRegions();
int  hitRow(int x, int y);           // selectable row 0..2 (= panel slot), -1 = none
}
