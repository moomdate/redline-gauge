#include "ui/v2/data_wave_ui.h"
#include "ui/canvas.h"
#include "ui/theme.h"
#include "ui/gauge_model.h"
#include "config.h"
#include "assets/bg_datawave.h"
#include "assets/skin_icons.h"
#include "fonts/font_dw_speed.h"
#include "fonts/font_dw_gear.h"
#include "fonts/font_dw_val.h"
#include "fonts/font_rc_small.h"
#include "fonts/font_rc_unit.h"
#include "fonts/font_label.h"
#include "fonts/font_spd_unit.h"
#include "fonts/font_small.h"
#include "ui/i18n.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// DATA WAVE: the artwork (frames, carbon) is the mockup with its live data and slot labels removed
// (tools/gen_skin_slots.py -> bg_datawave). Every region re-composites from that plate, so only the
// live values, labels and the scrolling graphs are drawn here. Rows 2-4 show the three shared
// panel items (Settings::panelItem): tap a row to step to the next value that has data.
namespace data_wave_ui {

struct Rect { int x, y, w, h; };

static const Rect R_SPEED = {  10,  52, 108, 58 };
static const Rect R_GEAR  = {  30, 144,  40, 40 };
static const Rect R_MODE  = { 262,   3,  48, 17 };     // where the mockup had a clock
static const Rect R_SETUP = { 258, 209,  56, 26 };     // over the mockup's "TIME ATTACK" badge
static const Rect R_TRIP  = {   6, 207,  92, 17 };     // where the mockup had LAP
static const Rect R_UNIT  = {  50, 110,  46, 16 };     // speed unit under the number (km/h / mph)
static const Rect R_RPM   = { 133,  44,  68, 22 };
static const Rect R_ICON  = { 102, 157,  25, 26 };     // row 4 icon (temperature items)
static const Rect R_WAVE[4] = { { 203,  40, 106, 25 }, { 200,  81, 109, 26 },
                                { 182, 119, 127, 24 }, { 182, 161, 127, 21 } };
// selectable rows (slot 0..2 = screen rows 2..4): label line, value box, text anchors
struct Row { Rect label, val; int lx, lby, vx, vby; };   // vx = right edge of the value; unit fixed after it
static const Row ROW[3] = {
    { { 130,  73, 70, 12 }, { 120,  85, 79, 22 }, 136,  84, 174, 103 },
    { { 117, 112, 65, 12 }, { 120, 124, 62, 22 }, 123, 123, 158, 141 },
    { { 128, 149, 54, 12 }, { 127, 161, 55, 22 }, 136, 160, 158, 179 },
};
static const Rect R_HIT[3] = { { 110, 74, 210, 37 }, { 100, 112, 220, 37 }, { 95, 149, 225, 40 } };

enum { RG_SPEED, RG_UNIT, RG_GEAR, RG_MODE, RG_SETUP, RG_TRIP, RG_RPM, RG_ICON,
       RG_L0, RG_V0 = RG_L0 + 3, RG_W0 = RG_V0 + 3, RG_COUNT = RG_W0 + 4 };

// ---- graph history ----------------------------------------------------------------------------
static const int HIST = 128;
struct Wave { uint16_t color; uint8_t v[HIST]; uint8_t head, count; };   // v = 0..255 of full scale
static Wave s_wave[4];

static PushFn  s_push = nullptr;
static uint8_t s_units = 0;
static int     s_pushed = 0;
static char    s_key[RG_COUNT][28];
static uint8_t s_waveItem[3] = { 0xFF, 0xFF, 0xFF };   // item each graph is plotting

static inline Canvas &cv() { return gauge_ui::canvas(); }
static inline uint16_t C(uint32_t rgb) { return rgb565(rgb); }

static bool changed(int rg, const char *key) {
    if (!strcmp(s_key[rg], key)) return false;
    snprintf(s_key[rg], sizeof s_key[rg], "%s", key);
    return true;
}
static bool beginRegion(const Rect &r) { return cv().begin(r.x, r.y, r.w, r.h, bg_datawave); }
static void flush() { s_push(cv().x0, cv().y0, cv().w, cv().h, cv().px); s_pushed++; }

static void waveAdd(Wave &w, float frac) {
    if (frac < 0) frac = 0; else if (frac > 1) frac = 1;
    w.head = (uint8_t)((w.head + 1) % HIST);
    w.v[w.head] = (uint8_t)lroundf(frac * 255);
    if (w.count < HIST) w.count++;
}

// Thin bars every other column, fading up into the panel colour, with a bright glowing line on
// top -- the mockup's look. Newest sample at the right edge.
static void wavePaint(const Wave &w, const Rect &r) {
    const int base = r.y + r.h - 1;
    int prevTop = -1;
    for (int c = 0; c < r.w; c++) {
        int age = (r.w - 1) - c;
        if (age >= w.count) { prevTop = -1; continue; }
        int idx = (w.head - age + HIST * 2) % HIST;
        int bh = 1 + w.v[idx] * (r.h - 3) / 255;
        int top = base - bh, x = r.x + c;
        if ((c & 1) == 0)
            for (int y = top + 1; y <= base; y++)
                cv().blendPixel(x, y, w.color, (uint8_t)(230 - 150 * (y - top) / (bh + 1)));   // bright under the line, dimmer at the foot
        // glow line: join to the previous column so steep edges stay continuous
        int a = prevTop < 0 ? top : prevTop, b = top;
        if (a > b) { int t = a; a = b; b = t; }
        for (int y = a; y <= b; y++) cv().blendPixel(x, y, shade565(w.color, 0.45f), 230);
        cv().blendPixel(x, top - 1, w.color, 110);
        prevTop = top;
    }
}

// Copy an icon keyed: only pixels clearly brighter than the dark tile behind it.
static void drawIcon(const SkinIcon &ic, int x, int y) {
    for (int j = 0; j < ic.h; j++)
        for (int i = 0; i < ic.w; i++) {
            uint16_t p = ic.px[j * ic.w + i];
            int r = (p >> 11) << 3, g = ((p >> 5) & 63) << 2, b = (p & 31) << 3;
            int m = r > g ? (r > b ? r : b) : (g > b ? g : b);
            if (m > 70) cv().pixel(x + i, y + j, p);
            else if (m > 40) cv().blendPixel(x + i, y + j, p, (uint8_t)((m - 40) * 8));
        }
}

// Three red slanted stripes after a label, as in the mockup.
static void stripes(int x, int baseline) {
    const uint16_t red = C(0xE8201C);
    for (int k = 0; k < 3; k++)
        for (int j = 0; j < 6; j++) {
            float xa = x + k * 5 + (5 - j) * 0.6f;
            cv().hspan(xa, xa + 2.6f, baseline - 6 + j, red);
        }
}

// ---- live values --------------------------------------------------------------------------------
static void drawSpeed(const GaugeView &v) {
    char num[8];
    if (v.speedValid) snprintf(num, sizeof num, "%d", displaySpeed(v.speed, s_units)); else strcpy(num, "--");
    if (!changed(RG_SPEED, num) || !beginRegion(R_SPEED)) return;
    cv().text(font_dw_speed, 58, 108, num, C(v.speedValid ? 0xF4F6F8 : 0x6f808b), ALIGN_CENTER, 1, true);
    flush();
}

static void drawUnit() {
    const char *u = s_units ? "mph" : "km/h";
    if (!changed(RG_UNIT, u) || !beginRegion(R_UNIT)) return;
    cv().text(font_spd_unit, 73, 121, u, C(0xE6ECEF), ALIGN_CENTER, 1);
    flush();
}

static void drawGear(const GaugeView &v) {
    char g[4] = "-";
    if (v.gear == 0) strcpy(g, "N"); else if (v.gear > 0) snprintf(g, sizeof g, "%d", v.gear);
    if (!changed(RG_GEAR, g) || !beginRegion(R_GEAR)) return;
    cv().text(font_dw_gear, 50, 178, g, C(0xFF2A1E), ALIGN_CENTER, 0, true);
    flush();
}

static void drawMode(const GaugeView &v) {
    const char *m = v.mode ? v.mode : "";
    if (!changed(RG_MODE, m) || !beginRegion(R_MODE)) return;
    cv().text(font_small, 308, 15, m, C(0xC9D2D8), ALIGN_RIGHT);
    flush();
}

// SETUP button: dark chip with a red rim, same corner as the default skin's button.
static void drawSetup() {
    char key[4]; snprintf(key, sizeof key, "%u", i18n::lang);
    if (!changed(RG_SETUP, key) || !beginRegion(R_SETUP)) return;
    const Rect &r = R_SETUP;
    const uint16_t red = C(0xE8201C);
    cv().fillRect(r.x, r.y, r.w, r.h, C(0x0B0D10));     // opaque: hides the baked "TIME ATTACK"
    cv().fillRect(r.x, r.y, r.w, 1, red);
    cv().fillRect(r.x, r.y + r.h - 1, r.w, 1, shade565(red, -0.5f));
    cv().fillRect(r.x, r.y, 1, r.h, shade565(red, -0.3f));
    cv().fillRect(r.x + r.w - 1, r.y, 1, r.h, shade565(red, -0.3f));
    cv().text(i18n::font(font_small), r.x + r.w / 2, r.y + 17, i18n::T("SETUP", "ตั้งค่า"),
              C(0xE6ECEF), ALIGN_CENTER, 1);
    flush();
}

static void drawTrip(const GaugeView &v) {
    char val[12]; snprintf(val, sizeof val, "%.1f", s_units ? v.tripKm * 0.621371f : v.tripKm);
    char key[20]; snprintf(key, sizeof key, "%s%u%u", val, i18n::lang, s_units);
    if (!changed(RG_TRIP, key) || !beginRegion(R_TRIP)) return;
    int x = 12;
    x += cv().text(i18n::font(font_label), x, 219, i18n::T("TRIP", "ระยะทาง"), C(0xE6ECEF), ALIGN_LEFT, 1);
    x += 6;
    x += cv().text(font_rc_small, x, 220, val, C(0xF4F6F8), ALIGN_LEFT, 0, true);
    cv().text(font_rc_unit, x + 3, 220, s_units ? "mi" : "km", C(0xC9D2D8), ALIGN_LEFT);
    flush();
}

static void drawRpm(const GaugeView &v) {
    char val[12];
    if (v.rpmValid) snprintf(val, sizeof val, "%d", v.rpmText); else strcpy(val, "--");
    if (!changed(RG_RPM, val) || !beginRegion(R_RPM)) return;
    cv().text(font_dw_val, R_RPM.x + 4, R_RPM.y + 19, val, C(v.rpmValid ? 0xF4F6F8 : 0x6f808b), ALIGN_LEFT, 0, true);
    flush();
}

static void drawRow(int j, const GaugeView &v) {
    const uint8_t item = gauge_ui::panelItem(j);
    gauge_ui::ItemInfo in; gauge_ui::itemInfo(item, v, in);
    const Row &R = ROW[j];
    char key[28];

    snprintf(key, sizeof key, "%u|%u|%s", item, i18n::lang, in.label);
    if (changed(RG_L0 + j, key) && beginRegion(R.label)) {
        const char *lab = gauge_ui::itemLabel(in.label);
        int w = cv().text(i18n::font(font_label), R.lx, R.lby, lab, C(0xE6ECEF), ALIGN_LEFT, 1);
        if (R.lx + w + 6 + 17 <= R.label.x + R.label.w) stripes(R.lx + w + 6, R.lby);   // only when all 3 fit
        flush();
    }

    snprintf(key, sizeof key, "%u|%s", item, in.value);
    if (changed(RG_V0 + j, key) && beginRegion(R.val)) {
        // right-aligned on a fixed edge, unit at a fixed x: digits coming and going never move it
        cv().text(font_dw_val, R.vx, R.vby, in.value, C(in.valid ? 0xF4F6F8 : 0x6f808b), ALIGN_RIGHT, 0, true);
        if (in.valid && in.unit[0]) cv().text(font_rc_unit, R.vx + 3, R.vby - 1, in.unit, C(0xC9D2D8), ALIGN_LEFT);
        flush();
    }

    if (j == 2) {   // the bottom row keeps the mockup's thermometer for temperatures
        bool icon = in.icon == gauge_ui::ICON_THERM || in.icon == gauge_ui::ICON_OIL;
        if (changed(RG_ICON, icon ? "t" : "-") && beginRegion(R_ICON)) {
            if (icon) drawIcon(icon_therm_s, 103, 158);
            flush();
        }
    }

    // a row switched to another item: its graph starts over
    if (s_waveItem[j] != item) { s_waveItem[j] = item; s_wave[j + 1].count = 0; s_key[RG_W0 + j + 1][0] = 1; }
}

static void drawWave(int i) {
    char key[8]; snprintf(key, sizeof key, "%u.%u", s_wave[i].head, s_wave[i].count ? 1 : 0);
    if (!changed(RG_W0 + i, key) || !beginRegion(R_WAVE[i])) return;
    wavePaint(s_wave[i], R_WAVE[i]);
    flush();
}

static float fracFor(int i, const GaugeView &v) {
    if (i == 0) return v.rpmValid ? v.rpmBar / (float)RPM_MAX : 0;
    gauge_ui::ItemInfo in; gauge_ui::itemInfo(gauge_ui::panelItem(i - 1), v, in);
    return in.frac;
}

// ---- public -----------------------------------------------------------------------------------
void setUnits(uint8_t units) { s_units = units ? 1 : 0; s_key[RG_SPEED][0] = s_key[RG_UNIT][0] = s_key[RG_TRIP][0] = 1; }

void setTheme(const Theme &) {
    // The plate is red-on-carbon whatever the colour theme; graph colours follow the mockup.
    static const uint32_t col[4] = { 0xFF2A1E, 0x1EC8FF, 0xFFC21A, 0x2AD4FF };
    for (int i = 0; i < 4; i++) s_wave[i].color = C(col[i]);
    redrawAll();
}

void begin(PushFn push, const Theme &theme) {
    s_push = push;
    for (int i = 0; i < 4; i++) { s_wave[i].head = 0; s_wave[i].count = 0; }
    setTheme(theme);
}

void redrawAll() {
    Canvas &c = cv();
    for (int y = 0; y < 240; y += 40) { c.begin(0, y, 320, 40, bg_datawave); s_push(0, y, 320, 40, c.px); }
    invalidate();
}

void invalidate() { for (int i = 0; i < RG_COUNT; i++) s_key[i][0] = 1; }

int lastPushedRegions() { return s_pushed; }

int hitRow(int x, int y) {
    for (int j = 0; j < 3; j++) {
        const Rect &r = R_HIT[j];
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return j;
    }
    return -1;
}

static uint32_t s_frame = 0;
void render(const GaugeView &v) {
    s_pushed = 0;
    for (int j = 0; j < 3; j++) drawRow(j, v);   // first: may restart a graph
    int ch = s_frame++ & 3;                       // one graph sampled per frame: ~15 Hz each
    waveAdd(s_wave[ch], fracFor(ch, v));
    drawSpeed(v);
    drawUnit();
    drawGear(v);
    drawMode(v);
    drawSetup();
    drawTrip(v);
    drawRpm(v);
    for (int i = 0; i < 4; i++) drawWave(i);
}

}  // namespace data_wave_ui
