#pragma once
#include "ui/gauge_ui.h"

// RACE CODE skin: round rev dial with a lit arc, speed in the hub, temp / battery / trip tiles.
namespace race_code_ui {
void begin(PushFn push, const Theme &theme);
void setTheme(const Theme &theme);   // full repaint (plate colours are fixed)
void setUnits(uint8_t units);
void redrawAll();
void invalidate();
void render(const GaugeView &v);
int  lastPushedRegions();
bool hitSetup(int x, int y);
int  hitTile(int x, int y);          // left tile 0..2 (= panel slot), -1 = none
}
