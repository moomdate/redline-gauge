#include "ui/settings_ui.h"
#include "ui/canvas.h"
#include "ui/gauge_ui.h"
#include "ui/theme.h"
#include "config.h"
#include "fonts/font_ui.h"
#include "fonts/font_small.h"
#include <stdio.h>
#include <string.h>

namespace settings_ui {

const char *const kSourceLabels[SRC_COUNT] = { "SIM", "TOUCH", "SERIAL", "OBD BT", "CUSTOM", "HONDA", "H TEST" };

// ---- layout ----------------------------------------------------------------------
struct Rect { int x, y, w, h; };
static bool inside(const Rect &r, int x, int y) {
    // resistive touch is ±3 px on a good day: give every button a small margin
    return x >= r.x - 3 && x < r.x + r.w + 3 && y >= r.y - 3 && y < r.y + r.h + 3;
}

static const Rect R_DONE = { 250, 4, 64, 22 };
static const Rect R_TIMER = { 178, 4, 66, 22 };   // drag timer screen
static const Rect R_COLORS = { 102, 4, 72, 22 };  // flip panel colour inversion
static Rect themeCard(int i)  { return { 8 + i * 104, 46, 96, 72 }; }
static Rect sourceBtn(int i)  { return { 6 + i * 44, 152, 42, 22 }; }
static const Rect R_SHIFT_DN = {   8, 194, 26, 22 };
static const Rect R_SHIFT_V  = {  36, 194, 78, 22 };
static const Rect R_SHIFT_UP = { 116, 194, 26, 22 };
static const Rect R_BRI_DN   = { 168, 194, 26, 22 };
static const Rect R_BRI_V    = { 196, 194, 78, 22 };
static const Rect R_BRI_UP   = { 276, 194, 26, 22 };
static const Rect R_BEEP     = {   8, 221, 70, 17 };
static const Rect R_PEAK     = {  82, 221, 70, 17 };
static const Rect R_PANELS   = { 156, 221, 70, 17 };
static const Rect R_GEAR     = { 230, 221, 72, 17 };
const char *const kPanelLabels[PANELS_COUNT] = { "PANEL AUTO", "PANEL STD", "PANEL HYB" };

#define C_TEXT   0xd9e2e7
#define C_MUTED  0x8796a0
#define C_BTN    0x141b21
#define C_EDGE   0x34434e

static inline uint16_t C(uint32_t rgb) { return rgb565(rgb); }

// Button with a chamfered top-left corner, echoing the angled panels in the art.
static void button(Canvas &cv, const Theme &t, const Rect &r, const char *label, bool on,
                   const GaugeFont &font = font_ui) {
    const int cut = 5;
    uint16_t fill = on ? blend565(90, C(t.accent), C(C_BTN)) : C(C_BTN);
    uint16_t edge = on ? C(t.accent) : C(C_EDGE);
    for (int y = 0; y < r.h; y++) {
        int inset = y < cut ? cut - y : 0;
        cv.fillRect(r.x + inset, r.y + y, r.w - inset, 1, fill);
        cv.pixel(r.x + inset, r.y + y, edge);                       // left edge / chamfer
        cv.pixel(r.x + r.w - 1, r.y + y, edge);
    }
    cv.fillRect(r.x + cut, r.y, r.w - cut, 1, edge);
    cv.fillRect(r.x, r.y + r.h - 1, r.w, 1, edge);
    if (on) cv.fillRect(r.x + 1, r.y + r.h - 3, r.w - 2, 2, C(t.accent));   // underline, like the web UI
    int base = r.y + (r.h + font.ascent) / 2 - (on ? 1 : 0);
    cv.text(font, r.x + r.w / 2, base, label, C(on ? t.accentBright : C_TEXT), ALIGN_CENTER, 1);
}

static void themeThumb(Canvas &cv, const Theme &cur, int i, bool selected) {
    Rect r = themeCard(i);
    const uint16_t *bg = kThemes[i].background;
    int y0 = r.y > cv.y0 ? r.y : cv.y0, y1 = r.y + r.h < cv.y0 + cv.h ? r.y + r.h : cv.y0 + cv.h;
    for (int y = y0; y < y1; y++) {
        int sy = (y - r.y) * 240 / r.h;
        for (int x = r.x; x < r.x + r.w; x++) {
            uint16_t c = bg[sy * 320 + (x - r.x) * 320 / r.w];
            cv.pixel(x, y, selected ? c : blend565(120, 0, c));
        }
    }
    uint16_t edge = selected ? C(cur.accent) : C(C_EDGE);
    for (int k = 1; k <= (selected ? 2 : 1); k++) {
        cv.fillRect(r.x - k, r.y - k, r.w + 2 * k, 1, edge);
        cv.fillRect(r.x - k, r.y + r.h + k - 1, r.w + 2 * k, 1, edge);
        cv.fillRect(r.x - k, r.y - k, 1, r.h + 2 * k, edge);
        cv.fillRect(r.x + r.w + k - 1, r.y - k, 1, r.h + 2 * k, edge);
    }
    // theme name + its accent swatch
    uint16_t sw = C(kThemes[i].accent);
    int nameW = cv.textWidth(font_ui, kThemes[i].name, 1);
    int nx = r.x + (r.w - nameW) / 2 + 5;
    cv.fillRect(nx - 10, 127, 6, 6, sw);
    cv.text(font_ui, nx, 134, kThemes[i].name, C(selected ? kThemes[i].accentBright : C_MUTED), ALIGN_LEFT, 1);
}

static bool s_peakCleared = false;     // RESET PEAK feedback, until the next tap / redraw

static void compose(Canvas &cv, const Settings &s) {
    const Theme &t = kThemes[s.theme];
    // backdrop: the theme's own art, darkened, so the page feels part of the gauge
    for (int i = 0; i < cv.w * cv.h; i++) cv.px[i] = blend565(215, 0, cv.px[i]);

    cv.text(font_ui, 12, 20, "SETTINGS", C(t.accentBright), ALIGN_LEFT, 2);
    cv.text(font_small, 312, 41, "v" REDLINE_VERSION, C(C_MUTED), ALIGN_RIGHT);
    cv.text(font_small, 266, 41, "BOOT BUTTON = TOUCH CAL", C(C_MUTED), ALIGN_RIGHT);
    button(cv, t, R_COLORS, "COLORS", false);
    button(cv, t, R_TIMER, "TIMER", false);
    button(cv, t, R_DONE, "DONE", true);
    for (int x = 8; x < 312; x++)                                   // accent rule fading out
        cv.blendPixel(x, 30, C(t.accent), (uint8_t)(255 - (x - 8) * 200 / 304));

    cv.text(font_small, 12, 41, "THEME", C(C_MUTED), ALIGN_LEFT, 1);
    for (int i = 0; i < THEME_COUNT; i++) themeThumb(cv, t, i, i == s.theme);

    cv.text(font_small, 12, 148, "DATA SOURCE", C(C_MUTED), ALIGN_LEFT, 1);
    // six sources: the compact font keeps every label clear of the button edges
    for (int i = 0; i < SRC_COUNT; i++) button(cv, t, sourceBtn(i), kSourceLabels[i], i == s.source, font_small);

    char buf[16];
    cv.text(font_small, 12, 190, "SHIFT LIGHT RPM", C(C_MUTED), ALIGN_LEFT, 1);
    button(cv, t, R_SHIFT_DN, "-", false);
    snprintf(buf, sizeof buf, "%d,%03d", s.shiftRpm / 1000, s.shiftRpm % 1000);
    button(cv, t, R_SHIFT_V, buf, false);
    button(cv, t, R_SHIFT_UP, "+", false);

#if BACKLIGHT_DIMMING                           // hidden: this CYD's backlight can't dim (config.h)
    cv.text(font_small, 172, 190, "BRIGHTNESS", C(C_MUTED), ALIGN_LEFT, 1);
    button(cv, t, R_BRI_DN, "-", false);
    snprintf(buf, sizeof buf, "%d%%", s.brightness);
    button(cv, t, R_BRI_V, buf, false);
    button(cv, t, R_BRI_UP, "+", false);
#endif

    button(cv, t, R_BEEP, s.beep ? "BEEP ON" : "BEEP OFF", s.beep, font_small);
    button(cv, t, R_PEAK, s_peakCleared ? "CLEARED" : "RESET PEAK", s_peakCleared, font_small);
    button(cv, t, R_PANELS, kPanelLabels[s.panels < PANELS_COUNT ? s.panels : 0], s.panels != PANELS_AUTO, font_small);
    button(cv, t, R_GEAR, s.gearMode == GEARMODE_OFF ? "GEAR OFF" : "GEAR AUTO", s.gearMode == GEARMODE_OFF, font_small);
}

void draw(const Settings &s) {
    Canvas &cv = gauge_ui::canvas();
    PushFn push = gauge_ui::pushFn();
    const uint16_t *bg = kThemes[s.theme].background;
    // 40-row strips: 320*40 fits the shared buffer; every element clips itself
    for (int y = 0; y < 240; y += 40) {
        cv.begin(0, y, 320, 40, bg);
        compose(cv, s);
        push(0, y, 320, 40, cv.px);
    }
}

Action tap(int x, int y, Settings &s) {
    Action a = ACT_NONE;
    bool wasCleared = s_peakCleared;
    s_peakCleared = false;
    if (inside(R_DONE, x, y)) return ACT_CLOSE;
    // checked before the neighbouring buttons, so a tap on an edge can't change both
    if (inside(R_PEAK, x, y)) {
        s_peakCleared = true;
        draw(s);                               // "CLEARED" lit, so the tap visibly worked
        return ACT_RESET_PEAK;
    }
    if (inside(R_TIMER, x, y)) return ACT_TIMER;
    if (inside(R_COLORS, x, y)) { s.invert = !s.invert; return ACT_CHANGED; }   // main applies it
    for (int i = 0; i < THEME_COUNT; i++) {
        Rect r = themeCard(i);
        r.h += 18;                                     // name row is tappable too
        if (inside(r, x, y) && s.theme != i) { s.theme = i; a = ACT_CHANGED; }
    }
    for (int i = 0; i < SRC_COUNT; i++)
        if (inside(sourceBtn(i), x, y) && s.source != i) { s.source = i; a = ACT_CHANGED; }

    if (inside(R_SHIFT_DN, x, y) && s.shiftRpm > Settings::kShiftMin) { s.shiftRpm -= Settings::kShiftStep; a = ACT_CHANGED; }
    if (inside(R_SHIFT_UP, x, y) && s.shiftRpm < Settings::kShiftMax) { s.shiftRpm += Settings::kShiftStep; a = ACT_CHANGED; }
#if BACKLIGHT_DIMMING
    if (inside(R_BRI_DN, x, y) && s.brightness > Settings::kBrightMin) { s.brightness -= Settings::kBrightStep; a = ACT_CHANGED; }
    if (inside(R_BRI_UP, x, y) && s.brightness < 100) { s.brightness += Settings::kBrightStep; a = ACT_CHANGED; }
#endif
    if (inside(R_BEEP, x, y)) { s.beep = !s.beep; a = ACT_CHANGED; }
    if (inside(R_PANELS, x, y)) { s.panels = (s.panels + 1) % PANELS_COUNT; a = ACT_CHANGED; }
    if (inside(R_GEAR, x, y)) { s.gearMode = (s.gearMode + 1) % GEARMODE_COUNT; a = ACT_CHANGED; }
    if (a == ACT_CHANGED || wasCleared) draw(s);
    return a;
}

}  // namespace settings_ui
