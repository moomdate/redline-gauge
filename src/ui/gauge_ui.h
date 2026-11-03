#pragma once
// Renders a GaugeView onto the themed REDLINE background.
// The screen is split into independent regions (status, shift bar, rpm, speed,
// three side panels). Each frame, a region is re-composed and pushed ONLY if what
// it shows changed — so a steady cruise costs almost no SPI time.
#include <stdint.h>
#include "ui/gauge_model.h"
#include "ui/theme.h"
#include "data/link_status.h"
#include "settings.h"

class Canvas;

// Pushes a w*h block of plain RGB565 pixels to the screen at (x,y).
typedef void (*PushFn)(int x, int y, int w, int h, const uint16_t *pixels);

namespace gauge_ui {
void begin(PushFn push, const Theme &theme);
void setTheme(const Theme &theme);   // swap background + accents, full redraw
const Theme &theme();
void redrawAll();                    // repaint background, then every region next render()
void invalidate();                   // force every region to redraw next frame
void render(const GaugeView &v);     // redraw regions whose content changed
// Link pill on the right of the status bar for sources with an ECU link (HONDA K, OBD BT);
// nullptr = plain status text.
void setLink(const LinkStatus *st, uint32_t now);
// What the three right-hand panels show (PanelItem each, from Settings::panelItem).
void setPanelItems(const uint8_t items[3]);
// Tap on a panel: which one (0..2), -1 = none. The hybrid panels (2 and 3 in HYBRID) are fixed.
int  hitPanel(int x, int y, const GaugeView &v);
// The item panel `i` steps to on a tap: the next one with live data that no other panel shows
// (any next one when nothing has data, e.g. before the link is up). `avail` (bit n = PanelItem n)
// replaces "has live data" with "the source can supply it"; 0 = judge by live data.
uint8_t nextPanelItem(int i, const GaugeView &v, uint32_t avail = 0);
int  lastPushedRegions();            // how many regions the last render() pushed
Canvas &canvas();                    // shared scratch canvas (also used by the settings screen)
PushFn  pushFn();

// Touch zones (screen coordinates)
bool hitStatus(int x, int y);
bool hitLink(int x, int y);          // the link pill (only while one is shown)
bool hitMain(int x, int y);
bool hitSetup(int x, int y);         // the SETUP button, bottom-right
}
