#include "ui/v2/race_code_ui.h"
#include "ui/canvas.h"
#include "ui/theme.h"
#include "ui/gauge_model.h"
#include "config.h"
#include "assets/bg_racecode.h"
#include "fonts/font_rc_speed.h"
#include "fonts/font_rc_label.h"
#include "fonts/font_rc_small.h"
#include "fonts/font_dw_gear.h"
#include "fonts/font_dw_val.h"
#include "fonts/font_small.h"
#include "fonts/font_label.h"
#include "fonts/font_rc_unit.h"
#include "assets/skin_icons.h"
#include "ui/i18n.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// RACE CODE: the mockup with its live data removed and the rev arc switched off is the plate
// (bg_racecode); bg_racecode_lit holds the same arc fully lit. Each frame the arc pixels up to
// the rpm angle are copied from the lit band, segment by segment, only where the angle moved.
// See tools/gen_racecode_plate.py.
namespace race_code_ui {

struct Rect { int x, y, w, h; };

static const Rect R_SPEED = { 128, 103, 112, 49 };
static const Rect R_RPM   = { 155,  74,  68, 21 };
static const Rect R_GEAR  = { 160, 176,  38, 34 };
// Left tiles: the three shared panel items (Settings::panelItem). Tap a tile for the next value.
static const Rect R_TILE0 = {   8,  54,  77, 33 };
static const Rect R_TILE1 = {   8,  98,  73, 33 };
static const Rect R_TILE2 = {   8, 142,  82, 36 };
// Usable inside of each tile (between the slanted rim and the dial's chrome) and text baselines.
struct Tile { int xl, xr, labelBase, valBase, iconCy; };
static const Tile TILE[3] = { { 11, 79,  64,  83,  71 },
                              { 10, 76, 108, 127, 114 },
                              { 13, 85, 153, 172, 160 } };
static const Rect R_TRIP  = {  50, 192,  49, 16 };
static const Rect R_MODE  = { 258,   6,  56, 18 };     // the mockup's clock box
static const Rect R_SETUP = { 226, 192,  78, 20 };     // inside the mockup's SPORT badge

enum { RG_SPEED, RG_RPM, RG_GEAR, RG_T0, RG_T1, RG_T2, RG_TRIP, RG_MODE, RG_SETUP, RG_COUNT };
static const Rect *const kRect[RG_COUNT] = { &R_SPEED, &R_RPM, &R_GEAR, &R_TILE0, &R_TILE1, &R_TILE2,
                                             &R_TRIP, &R_MODE, &R_SETUP };

// Dial calibration: angle (deg, 0 = up, clockwise) of the numerals 0..9 (x1000 rpm) on the plate.
static const float kNumA[10] = { -136.4f, -85.3f, -57.6f, -33.1f, -1.9f, 28.6f, 59.4f, 84.0f,
                                 115.6f, 133.6f };

// Arc segments: precomputed bounding boxes of the band pixels in each angular slice.
static const int NSEG = 54;
struct SegBox { int16_t x0, y0, x1, y1; };     // inclusive; x0 > x1 = empty
static SegBox s_seg[NSEG];
static float  s_arcA = -200;                        // angle currently drawn (deg)
static bool   s_arcDirty = true;

static PushFn  s_push = nullptr;
static uint8_t s_units = 0;
static int     s_pushed = 0;
static char    s_key[RG_COUNT][32];

static inline Canvas &cv() { return gauge_ui::canvas(); }
static inline uint16_t C(uint32_t rgb) { return rgb565(rgb); }

static bool changed(int rg, const char *key) {
    if (!strcmp(s_key[rg], key)) return false;
    snprintf(s_key[rg], sizeof s_key[rg], "%s", key);
    return true;
}
static void flush() { s_push(cv().x0, cv().y0, cv().w, cv().h, cv().px); s_pushed++; }
static void litOver(float upTo);
static bool beginRegion(const Rect &r) {
    if (!cv().begin(r.x, r.y, r.w, r.h, bg_racecode)) return false;
    litOver(s_arcA);                               // keep the arc lit where a value box crosses it
    return true;
}

// ---- rev arc ---------------------------------------------------------------------------------
static const float SEG_DEG = (RC_A1 + 4 - (RC_A0 - 4)) / NSEG;
static inline float segStart(int i) { return RC_A0 - 4 + i * SEG_DEG; }

static inline bool bandPixel(int x, int y, float *ang) {
    float dx = x - RC_CX, dy = y - RC_CY, r2 = dx * dx + dy * dy;
    if (r2 < RC_R_IN * RC_R_IN || r2 > RC_R_OUT * RC_R_OUT) return false;
    *ang = atan2f(dx, -dy) * (180.0f / (float)M_PI);
    return true;
}
static inline uint16_t litPx(int x, int y) {
    return bg_racecode_lit[(y - RC_LIT_Y) * RC_LIT_W + (x - RC_LIT_X)];
}

static void buildSegments() {
    for (int i = 0; i < NSEG; i++) s_seg[i] = { 32767, 32767, -1, -1 };
    for (int y = RC_LIT_Y; y < RC_LIT_Y + RC_LIT_H; y++)
        for (int x = RC_LIT_X; x < RC_LIT_X + RC_LIT_W; x++) {
            float a;
            if (litPx(x, y) == bg_racecode[y * 320 + x] || !bandPixel(x, y, &a)) continue;
            int i = (int)((a - segStart(0)) / SEG_DEG);
            if (i < 0 || i >= NSEG) continue;
            SegBox &b = s_seg[i];
            if (x < b.x0) b.x0 = x;
            if (x > b.x1) b.x1 = x;
            if (y < b.y0) b.y0 = y;
            if (y > b.y1) b.y1 = y;
        }
}

static bool overlaps(const SegBox &b, const Rect &r) {
    return b.x0 < r.x + r.w && b.x1 >= r.x && b.y0 < r.y + r.h && b.y1 >= r.y;
}

// Recomposite one segment's box: plate everywhere, lit band where the pixel's angle <= upTo.
// Over the canvas's current box: lit-band pixels whose angle <= upTo.
static void litOver(float upTo) {
    Canvas &c = cv();
    uint16_t *p = c.px;
    for (int y = c.y0; y < c.y0 + c.h; y++)
        for (int x = c.x0; x < c.x0 + c.w; x++, p++) {
            float a;
            if (x < RC_LIT_X || x >= RC_LIT_X + RC_LIT_W || y < RC_LIT_Y || y >= RC_LIT_Y + RC_LIT_H) continue;
            if (bandPixel(x, y, &a) && a <= upTo) *p = litPx(x, y);
        }
}

static void drawSeg(int i, float upTo) {
    const SegBox &b = s_seg[i];
    if (b.x0 > b.x1) return;
    if (!cv().begin(b.x0, b.y0, b.x1 - b.x0 + 1, b.y1 - b.y0 + 1, bg_racecode)) return;
    litOver(upTo);
    flush();
    for (int rg = 0; rg < RG_COUNT; rg++)          // a box that clipped a value: redraw the value
        if (overlaps(b, *kRect[rg])) s_key[rg][0] = 1;
}

static float rpmAngle(float rpm) {
    if (rpm <= 0) return RC_A0 - 6;                // nothing lit
    float k = rpm / 1000.0f;
    if (k >= 9) return kNumA[9] + 2;
    int i = (int)k;
    return kNumA[i] + (kNumA[i + 1] - kNumA[i]) * (k - i);
}

static void drawArc(const GaugeView &v) {
    float target = rpmAngle(v.rpmValid ? v.rpmBar : 0);
    if (s_arcDirty) {                              // after a full repaint: every lit segment
        s_arcDirty = false;
        for (int i = 0; i < NSEG; i++)
            if (segStart(i) <= target) drawSeg(i, target);
        s_arcA = target;
        return;
    }
    if (fabsf(target - s_arcA) < 0.5f) return;
    const float maxStep = 12 * SEG_DEG;            // cap the work per frame; catches up next frame
    if (target > s_arcA + maxStep) target = s_arcA + maxStep;
    if (target < s_arcA - maxStep) target = s_arcA - maxStep;
    float lo = fminf(s_arcA, target), hi = fmaxf(s_arcA, target);
    for (int i = 0; i < NSEG; i++) {
        float a0 = segStart(i), a1 = a0 + SEG_DEG;
        if (a1 + 2 >= lo && a0 - 2 <= hi) drawSeg(i, target);   // +-2: boxes overlap neighbours
    }
    s_arcA = target;
}

// ---- live values -------------------------------------------------------------------------------
static const uint32_t WHITE = 0xF4F6F8, DIM = 0x6F808B;

static void drawText(int rg, const char *s, bool valid, const GaugeFont &f, int x, int y,
                     TextAlign al, uint32_t col = WHITE) {
    char key[32]; snprintf(key, sizeof key, "%s", s);
    if (!changed(rg, key) || !beginRegion(*kRect[rg])) return;
    cv().text(f, x, y, s, C(valid ? col : DIM), al, 0, true);
    flush();
}

// Copy an icon keyed: only pixels clearly brighter than the dark tile behind it.
static void drawIcon(const SkinIcon &ic, int cx, int cy) {
    const int x = cx - ic.w / 2, y = cy - ic.h / 2;
    for (int j = 0; j < ic.h; j++)
        for (int i = 0; i < ic.w; i++) {
            uint16_t p = ic.px[j * ic.w + i];
            int r = (p >> 11) << 3, g = ((p >> 5) & 63) << 2, b = (p & 31) << 3;
            int m = r > g ? (r > b ? r : b) : (g > b ? g : b);
            if (m > 70) cv().pixel(x + i, y + j, p);
            else if (m > 40) cv().blendPixel(x + i, y + j, p, (uint8_t)((m - 40) * 8));
        }
}

// Tile: item icon on the left, short label over the value, value + unit
// right-aligned. Every piece is measured so nothing overlaps; the icon always stays: a value too
// wide for the column drops to a smaller font, a Thai label too wide uses the English short name.
static void drawTile(int t, const GaugeView &v) {
    const uint8_t item = gauge_ui::panelItem(t);
    gauge_ui::ItemInfo in; gauge_ui::itemInfo(item, v, in);
    char key[32]; snprintf(key, sizeof key, "%u|%u|%s|%s", item, i18n::lang, in.value, in.shortLabel);
    if (!changed(RG_T0 + t, key) || !beginRegion(*kRect[RG_T0 + t])) return;
    const Tile &T = TILE[t];
    Canvas &c = cv();

    static const SkinIcon *const kIcon[] = { nullptr, &icon_oil, &icon_therm, &icon_batt, &icon_tps,
        &icon_afr, &icon_map, &icon_fuel, &icon_rate, &icon_load, &icon_ign, &icon_o2, &icon_trim };
    const SkinIcon *ic = in.icon < sizeof kIcon / sizeof kIcon[0] ? kIcon[in.icon] : nullptr;
    const GaugeFont &lf = i18n::font(font_label);
    const char *lab = gauge_ui::itemShortLabel(in.shortLabel);
    int lw = c.textWidth(lf, lab, 1);
    const int left = T.xl + (ic ? ic->w + 2 : 2);           // text column starts after the icon
    // the icon always stays: a Thai label too wide for the column falls back to the English one
    if (T.xr - lw < left && i18n::lang) { lab = in.shortLabel; lw = c.textWidth(font_label, lab, 1); }
    const GaugeFont &lfont = (lab == in.shortLabel) ? font_label : lf;

    const bool hasUnit = in.valid && in.unit[0];
    const int uw = hasUnit ? c.textWidth(font_rc_unit, in.unit) + 2 : 0;   // incl. the gap before it
    const GaugeFont *vf = &font_dw_val;
    // font picked from the item's widest value (fixed per item): it never flips size as the value moves
    if (T.xr - uw - c.textWidth(*vf, in.widest, 0, true) < left) vf = &font_rc_small;

    if (ic) drawIcon(*ic, T.xl + ic->w / 2, T.iconCy);
    const int vx = T.xr - uw;
    c.text(*vf, vx, T.valBase, in.value, C(in.valid ? WHITE : DIM), ALIGN_RIGHT, 0, true);
    if (hasUnit) c.text(font_rc_unit, vx + 2, T.valBase - 1, in.unit, C(0xAEB8BF), ALIGN_LEFT);
    c.text(lfont, T.xr - lw, T.labelBase, lab, C(0xC9D2D8), ALIGN_LEFT, 1);
    flush();
}

static void drawSetup() {
    char key[4]; snprintf(key, sizeof key, "%u", i18n::lang);
    if (!changed(RG_SETUP, key) || !beginRegion(R_SETUP)) return;
    cv().text(i18n::font(font_rc_label), R_SETUP.x + R_SETUP.w / 2, 209 - (i18n::lang ? 1 : 0),
              i18n::T("SETUP", "ตั้งค่า"), C(0xF4F6F8), ALIGN_CENTER, 1);
    flush();
}

// ---- public -----------------------------------------------------------------------------------
void setUnits(uint8_t units) { s_units = units ? 1 : 0; s_key[RG_SPEED][0] = 1; }
void setTheme(const Theme &) { redrawAll(); }

void begin(PushFn push, const Theme &theme) {
    s_push = push;
    buildSegments();
    setTheme(theme);
}

void redrawAll() {
    Canvas &c = cv();
    for (int y = 0; y < 240; y += 40) { c.begin(0, y, 320, 40, bg_racecode); s_push(0, y, 320, 40, c.px); }
    invalidate();
}

void invalidate() {
    for (int i = 0; i < RG_COUNT; i++) s_key[i][0] = 1;
    s_arcDirty = true;
}

int lastPushedRegions() { return s_pushed; }

bool hitSetup(int x, int y) { return x >= 218 && y >= 185 && y < 222; }

int hitTile(int x, int y) {
    for (int t = 0; t < 3; t++) {
        const Rect &r = *kRect[RG_T0 + t];
        if (x >= 0 && x < r.x + r.w + 6 && y >= r.y - 2 && y < r.y + r.h + 4) return t;
    }
    return -1;
}

void render(const GaugeView &v) {
    s_pushed = 0;

    drawArc(v);

    char b[16];
    if (v.speedValid) snprintf(b, sizeof b, "%d", displaySpeed(v.speed, s_units)); else strcpy(b, "--");
    drawText(RG_SPEED, b, v.speedValid, font_rc_speed, 184, 150, ALIGN_CENTER);

    if (v.rpmValid) snprintf(b, sizeof b, "%d", v.rpmText); else strcpy(b, "--");
    drawText(RG_RPM, b, v.rpmValid, font_dw_val, 189, 92, ALIGN_CENTER);

    strcpy(b, "-");
    if (v.gear == 0) strcpy(b, "N"); else if (v.gear > 0) snprintf(b, sizeof b, "%d", v.gear);
    drawText(RG_GEAR, b, true, font_dw_gear, 178, 208, ALIGN_CENTER, 0xFF2A1E);

    for (int t = 0; t < 3; t++) drawTile(t, v);

    snprintf(b, sizeof b, "%.1f", v.tripKm);
    drawText(RG_TRIP, b, true, font_rc_small, 97, 205, ALIGN_RIGHT);

    drawText(RG_MODE, v.mode ? v.mode : "", true, font_small, 310, 20, ALIGN_RIGHT, 0xC9D2D8);
    drawSetup();
}

}  // namespace race_code_ui
