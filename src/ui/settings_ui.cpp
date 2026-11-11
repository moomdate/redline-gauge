#include "ui/settings_ui.h"
#include "ui/canvas.h"
#include "ui/gauge_ui.h"
#include "ui/theme.h"
#include "config.h"
#include "fonts/font_ui.h"
#include "fonts/font_small.h"
#include "ui/i18n.h"
#include <stdio.h>
#include <string.h>

namespace settings_ui {

const char *const kSourceLabels[SRC_COUNT] = { "SIM", "TOUCH", "SERIAL", "OBD BT", "CUSTOM", "HONDA" };

// ---- layout ----------------------------------------------------------------------
struct Rect { int x, y, w, h; };
static bool inside(const Rect &r, int x, int y) {
    // resistive touch is ±3 px on a good day: give every button a small margin
    return x >= r.x - 3 && x < r.x + r.w + 3 && y >= r.y - 3 && y < r.y + r.h + 3;
}

static const Rect R_DONE = { 250, 4, 64, 22 };
static const Rect R_TIMER = { 178, 4, 66, 22 };   // drag timer screen
static const Rect R_COLORS = { 102, 4, 72, 22 };  // flip panel colour inversion
static const Rect R_SKIN     = {  12, 48, 152, 24 };   // main page: chip that opens the skin page
static const Rect R_THEME    = { 170, 48, 142, 24 };   // main page: chip that opens the theme page (DEFAULT skin only)
static const char *const kSkinNames[SKIN_COUNT] = { "DEFAULT", "DATA WAVE", "RACE CODE" };
static Rect themeCard(int i)  { return { 12, 50 + i * 56, 296, 48 }; }   // theme sub-page cards
static Rect skinCard(int i)   { return { 12, 50 + i * 56, 296, 48 }; }   // skin sub-page cards
static Rect sourceBtn(int i)  { return { 6 + i * 44, 96, 42, 20 }; }
static const Rect R_SHIFT_DN = {   8, 138, 24, 18 };
static const Rect R_SHIFT_V  = {  34, 138, 70, 18 };
static const Rect R_SHIFT_UP = { 106, 138, 24, 18 };
static const Rect R_BRI_DN   = { 168, 138, 24, 18 };
static const Rect R_BRI_V    = { 196, 138, 70, 18 };
static const Rect R_BRI_UP   = { 268, 138, 24, 18 };
// OPTIONS: a 4 x 2 grid of checkbox toggles
static const Rect R_BEEP     = {   8, 182, 71, 16 };
static const Rect R_GEAR     = {  83, 182, 71, 16 };
static const Rect R_UNITS    = { 158, 182, 71, 16 };
static const Rect R_LANG     = { 233, 182, 79, 16 };
static const Rect R_PANELS   = {   8, 200,  92, 16 };
static const Rect R_KUART    = { 103, 200,  70, 16 };
static const Rect R_PEAK     = { 176, 200,  70, 16 };
static const Rect R_ALERT    = { 249, 200,  63, 16 };
// ALERT sub-page (SETTINGS -> ALERT): master on/off + three threshold steppers + reset.
static uint8_t s_page = 0;                                 // 0 = main, 1 = alerts
static const Rect RA_MASTER  = {  12,  44, 150, 20 };
static Rect alDn(int row)  { return { 150, 72 + row * 42, 26, 24 }; }
static Rect alVal(int row) { return { 180, 72 + row * 42, 80, 24 }; }
static Rect alUp(int row)  { return { 264, 72 + row * 42, 26, 24 }; }
static const Rect RA_DEFAULT = {  12, 206, 150, 24 };
// HONDA model: a visible chip on the source row (only when HONDA is the source) that opens
// the model page, which holds two selectable model cards.
static const Rect R_HMODEL = { 186, 96, 54, 20 };
static Rect hmCard(int i) { return { 12, 58 + i * 64, 296, 52 }; }
// Sources shown in the picker (TOUCH and CUSTOM are hidden; still reachable over serial).
static const uint8_t kShownSrc[] = { SRC_SIM_AUTO, SRC_SERIAL, SRC_OBD, SRC_HONDA };
static const int kShownSrcN = (int)(sizeof kShownSrc / sizeof kShownSrc[0]);
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

// A checkbox toggle: state 1 = on (filled box), 0 = off (empty box), -1 = action (no box).
static void toggle(Canvas &cv, const Theme &t, const Rect &r, const char *label, int state) {
    uint16_t edge = state == 1 ? C(t.accent) : C(C_EDGE);
    uint16_t fill = state == 1 ? blend565(55, C(t.accent), C(C_BTN)) : C(C_BTN);
    cv.fillRect(r.x, r.y, r.w, r.h, fill);
    cv.fillRect(r.x, r.y, r.w, 1, edge);
    cv.fillRect(r.x, r.y + r.h - 1, r.w, 1, edge);
    cv.fillRect(r.x, r.y, 1, r.h, edge);
    cv.fillRect(r.x + r.w - 1, r.y, 1, r.h, edge);
    int tx = r.x + 7;
    if (state >= 0) {
        int bx = r.x + 6, by = r.y + (r.h - 10) / 2;
        cv.fillRect(bx, by, 10, 10, C(C_EDGE));
        cv.fillRect(bx + 1, by + 1, 8, 8, C(state ? 0 : C_BTN));   // on: dark base, then accent fill
        if (state) cv.fillRect(bx + 2, by + 2, 6, 6, C(t.accent));
        tx = r.x + 21;
    }
    cv.text(font_small, tx, r.y + (r.h + 7) / 2, label, C(state == 1 ? t.accentBright : C_TEXT), ALIGN_LEFT);
}


static bool s_peakCleared = false;     // RESET PEAK feedback, until the next tap / redraw

static void compose(Canvas &cv, const Settings &s) {
    const Theme &t = kThemes[s.theme];
    // backdrop: the theme's own art, darkened, so the page feels part of the gauge
    for (int i = 0; i < cv.w * cv.h; i++) cv.px[i] = blend565(215, 0, cv.px[i]);

    cv.text(i18n::font(font_ui), 12, 20, i18n::T("SETTINGS", "ตั้งค่า"), C(t.accentBright), ALIGN_LEFT, 2);
    cv.text(font_small, 312, 20, "v" REDLINE_VERSION, C(C_MUTED), ALIGN_RIGHT);
    button(cv, t, R_COLORS, "COLORS", false);
    button(cv, t, R_TIMER, "TIMER", false);
    button(cv, t, R_DONE, "DONE", true);
    for (int x = 8; x < 312; x++)                                   // accent rule fading out
        cv.blendPixel(x, 30, C(t.accent), (uint8_t)(255 - (x - 8) * 200 / 304));

    // SKIN: layout chip; tap to open the skin page.
    cv.text(font_small, R_SKIN.x, 41, "SKIN", C(C_MUTED), ALIGN_LEFT, 1);
    {
        const Rect &r = R_SKIN;
        cv.fillRect(r.x, r.y, r.w, r.h, C(C_BTN));
        cv.fillRect(r.x, r.y, r.w, 1, C(t.accent));
        cv.fillRect(r.x, r.y + r.h - 1, r.w, 1, C(t.accent));
        cv.fillRect(r.x, r.y, 1, r.h, C(t.accent));
        cv.fillRect(r.x + r.w - 1, r.y, 1, r.h, C(t.accent));
        cv.text(font_ui, r.x + 12, r.y + 17, kSkinNames[s.skin < SKIN_COUNT ? s.skin : 0],
                C(t.accentBright), ALIGN_LEFT, 1);
        cv.text(font_small, r.x + r.w - 9, r.y + 16, ">", C(C_MUTED), ALIGN_RIGHT);
    }
    // THEME: colours of the DEFAULT skin (the other skins have fixed artwork), so it only opens there.
    cv.text(font_small, R_THEME.x, 41, "THEME", C(C_MUTED), ALIGN_LEFT, 1);
    {
        const Rect &r = R_THEME;
        const bool on = s.skin == SKIN_DEFAULT;
        const uint16_t rim = on ? C(t.accent) : C(C_MUTED);
        cv.fillRect(r.x, r.y, r.w, r.h, C(C_BTN));
        cv.fillRect(r.x, r.y, r.w, 1, rim);
        cv.fillRect(r.x, r.y + r.h - 1, r.w, 1, rim);
        cv.fillRect(r.x, r.y, 1, r.h, rim);
        cv.fillRect(r.x + r.w - 1, r.y, 1, r.h, rim);
        if (on) {
            cv.fillRect(r.x + 10, r.y + 7, 10, 10, C(t.accent));                  // colour swatch
            cv.text(font_ui, r.x + 28, r.y + 17, t.name, C(t.accentBright), ALIGN_LEFT, 1);
            cv.text(font_small, r.x + r.w - 9, r.y + 16, ">", C(C_MUTED), ALIGN_RIGHT);
        } else {
            cv.text(i18n::font(font_small), r.x + r.w / 2, r.y + 16, i18n::T("DEFAULT SKIN ONLY", "ใช้กับสกิน DEFAULT"),
                    C(C_MUTED), ALIGN_CENTER, 1);
        }
    }

    cv.text(font_small, 12, 90, "DATA SOURCE", C(C_MUTED), ALIGN_LEFT, 1);
    // the compact font keeps every label clear of the button edges
    for (int j = 0; j < kShownSrcN; j++)
        button(cv, t, sourceBtn(j), kSourceLabels[kShownSrc[j]], kShownSrc[j] == s.source, font_small);
    if (s.source == SRC_HONDA) {           // bike model chip: tap to open the Wave / Giorno page
        cv.text(font_small, 184, 90, "MODEL", C(C_MUTED), ALIGN_LEFT, 1);
        button(cv, t, R_HMODEL, s.hondaModel == HM_GIORNO ? "GIORNO" : "WAVE", true, font_small);
    }

    char buf[16];
    cv.text(font_small, 12, 134, "SHIFT RPM", C(C_MUTED), ALIGN_LEFT, 1);
    button(cv, t, R_SHIFT_DN, "-", false);
    snprintf(buf, sizeof buf, "%d,%03d", s.shiftRpm / 1000, s.shiftRpm % 1000);
    button(cv, t, R_SHIFT_V, buf, false);
    button(cv, t, R_SHIFT_UP, "+", false);

#if BACKLIGHT_DIMMING                           // hidden: this CYD's backlight can't dim (config.h)
    cv.text(font_small, 172, 134, "BRIGHTNESS", C(C_MUTED), ALIGN_LEFT, 1);
    button(cv, t, R_BRI_DN, "-", false);
    snprintf(buf, sizeof buf, "%d%%", s.brightness);
    button(cv, t, R_BRI_V, buf, false);
    button(cv, t, R_BRI_UP, "+", false);
#endif

    cv.text(font_small, 12, 178, "OPTIONS", C(C_MUTED), ALIGN_LEFT, 1);
    toggle(cv, t, R_BEEP,  s.beep ? "BEEP ON" : "BEEP OFF", s.beep);
    toggle(cv, t, R_GEAR,  s.gearMode == GEARMODE_OFF ? "GEAR OFF" : "GEAR ON", s.gearMode != GEARMODE_OFF);
    toggle(cv, t, R_UNITS, s.units ? "MPH" : "KM/H", s.units);
    toggle(cv, t, R_LANG, s.lang ? "TH" : "EN", s.lang);
    toggle(cv, t, R_PANELS, kPanelLabels[s.panels < PANELS_COUNT ? s.panels : 0], s.panels != PANELS_AUTO);
    toggle(cv, t, R_KUART, s.klineUart == 0 ? "K-UART 0" : "K-UART 2", s.klineUart == 0);
    toggle(cv, t, R_PEAK,  s_peakCleared ? "CLEARED" : "RESET PEAK", -1);
    toggle(cv, t, R_ALERT, s.alerts ? "ALERT ON" : "ALERT OFF", -1);   // tap opens the ALERT page
}

// SETTINGS -> ALERT: toggle the warnings and set each trigger point.
static void composeAlerts(Canvas &cv, const Settings &s) {
    const Theme &t = kThemes[s.theme];
    for (int i = 0; i < cv.w * cv.h; i++) cv.px[i] = blend565(215, 0, cv.px[i]);

    cv.text(i18n::font(font_ui), 12, 20, i18n::T("ALERTS", "แจ้งเตือน"), C(t.accentBright), ALIGN_LEFT, 2);
    button(cv, t, R_DONE, i18n::T("BACK", "กลับ"), true, i18n::font(font_ui));
    for (int x = 8; x < 312; x++)
        cv.blendPixel(x, 30, C(t.accent), (uint8_t)(255 - (x - 8) * 200 / 304));

    toggle(cv, t, RA_MASTER, s.alerts ? "WARNINGS ON" : "WARNINGS OFF", s.alerts);

    static const char *en[3] = { "OVERHEAT", "LOW BATTERY", "INTAKE HOT" };
    static const char *th[3] = { "ความร้อนเกิน", "แบตอ่อน", "ไอดีร้อน" };
    char buf[16];
    for (int r = 0; r < 3; r++) {
        int y = 72 + r * 42;
        cv.text(i18n::font(font_small), 12, y + 16, i18n::T(en[r], th[r]), C(C_TEXT), ALIGN_LEFT, 1);
        button(cv, t, alDn(r), "-", false);
        if (r == 0)      snprintf(buf, sizeof buf, "%d\xC2\xB0""C", s.alertCoolant);
        else if (r == 1) snprintf(buf, sizeof buf, "%d.%dV", s.alertVoltLo / 10, s.alertVoltLo % 10);
        else             snprintf(buf, sizeof buf, "%d\xC2\xB0""C", s.alertIat);
        button(cv, t, alVal(r), buf, false, font_small);
        button(cv, t, alUp(r), "+", false);
    }
    button(cv, t, RA_DEFAULT, i18n::T("RESET DEFAULT", "ค่าเริ่มต้น"), false, i18n::font(font_small));
}

// SETTINGS -> (tap HONDA again): pick the bike model. Sets the temp label and default shift.
static void composeHonda(Canvas &cv, const Settings &s) {
    const Theme &t = kThemes[s.theme];
    for (int i = 0; i < cv.w * cv.h; i++) cv.px[i] = blend565(215, 0, cv.px[i]);
    cv.text(i18n::font(font_ui), 12, 20, i18n::T("HONDA MODEL", "รุ่น Honda"), C(t.accentBright), ALIGN_LEFT, 2);
    button(cv, t, R_DONE, i18n::T("BACK", "กลับ"), true, i18n::font(font_ui));
    for (int x = 8; x < 312; x++)
        cv.blendPixel(x, 30, C(t.accent), (uint8_t)(255 - (x - 8) * 200 / 304));

    static const char *name[2] = { "WAVE 110", "GIORNO / LEAD 125" };
    static const char *en[2] = { "air-cooled   OIL TEMP   shift 8500",
                                 "liquid-cooled   COOLANT   shift 9000" };
    static const char *th[2] = { "ระบายอากาศ   อุณหภูมิน้ำมัน   shift 8500",
                                 "ระบายน้ำ   หล่อเย็น   shift 9000" };
    for (int i = 0; i < 2; i++) {
        Rect r = hmCard(i);
        bool sel = s.hondaModel == i;
        cv.fillRect(r.x, r.y, r.w, r.h, sel ? blend565(70, C(t.accent), C(C_BTN)) : C(C_BTN));
        uint16_t edge = sel ? C(t.accent) : C(C_EDGE);
        cv.fillRect(r.x, r.y, r.w, 1, edge);
        cv.fillRect(r.x, r.y + r.h - 1, r.w, 1, edge);
        cv.fillRect(r.x, r.y, 1, r.h, edge);
        cv.fillRect(r.x + r.w - 1, r.y, 1, r.h, edge);
        if (sel) cv.fillRect(r.x + 1, r.y + r.h - 3, r.w - 2, 2, C(t.accent));
        cv.text(font_ui, r.x + 14, r.y + 23, name[i], C(sel ? t.accentBright : C_TEXT), ALIGN_LEFT, 1);
        cv.text(i18n::font(font_small), r.x + 14, r.y + 42, i18n::T(en[i], th[i]), C(C_MUTED), ALIGN_LEFT, 1);
    }
}

// SETTINGS -> (tap the THEME chip): pick the colour theme. No live preview, just swatch + name.
static void composeTheme(Canvas &cv, const Settings &s) {
    const Theme &t = kThemes[s.theme];
    for (int i = 0; i < cv.w * cv.h; i++) cv.px[i] = blend565(215, 0, cv.px[i]);
    cv.text(i18n::font(font_ui), 12, 20, i18n::T("THEME", "ธีมสี"), C(t.accentBright), ALIGN_LEFT, 2);
    button(cv, t, R_DONE, i18n::T("BACK", "กลับ"), true, i18n::font(font_ui));
    for (int x = 8; x < 312; x++)
        cv.blendPixel(x, 30, C(t.accent), (uint8_t)(255 - (x - 8) * 200 / 304));
    for (int i = 0; i < THEME_COUNT; i++) {
        Rect r = themeCard(i);
        const Theme &th = kThemes[i];
        bool sel = s.theme == i;
        cv.fillRect(r.x, r.y, r.w, r.h, sel ? blend565(70, C(th.accent), C(C_BTN)) : C(C_BTN));
        uint16_t edge = sel ? C(th.accent) : C(C_EDGE);
        cv.fillRect(r.x, r.y, r.w, 1, edge);
        cv.fillRect(r.x, r.y + r.h - 1, r.w, 1, edge);
        cv.fillRect(r.x, r.y, 1, r.h, edge);
        cv.fillRect(r.x + r.w - 1, r.y, 1, r.h, edge);
        if (sel) cv.fillRect(r.x + 1, r.y + r.h - 3, r.w - 2, 2, C(th.accent));
        cv.fillRect(r.x + 16, r.y + r.h / 2 - 9, 18, 18, C(th.accent));   // colour swatch
        cv.text(font_ui, r.x + 46, r.y + r.h / 2 + 6, th.name, C(sel ? th.accentBright : C_TEXT), ALIGN_LEFT, 2);
    }
}

// SETTINGS -> (tap the SKIN chip): pick the gauge layout.
static void composeSkin(Canvas &cv, const Settings &s) {
    const Theme &t = kThemes[s.theme];
    for (int i = 0; i < cv.w * cv.h; i++) cv.px[i] = blend565(215, 0, cv.px[i]);
    cv.text(i18n::font(font_ui), 12, 20, i18n::T("SKIN", "สกิน"), C(t.accentBright), ALIGN_LEFT, 2);
    button(cv, t, R_DONE, i18n::T("BACK", "กลับ"), true, i18n::font(font_ui));
    for (int x = 8; x < 312; x++)
        cv.blendPixel(x, 30, C(t.accent), (uint8_t)(255 - (x - 8) * 200 / 304));
    static const char *en[SKIN_COUNT] = { "the original gauge", "scrolling data graphs", "round rev dial" };
    static const char *th[SKIN_COUNT] = { "เกจแบบเดิม", "กราฟข้อมูลวิ่งเป็นคลื่น", "หน้าปัดวงกลม" };
    for (int i = 0; i < SKIN_COUNT; i++) {
        Rect r = skinCard(i);
        bool sel = s.skin == i;
        cv.fillRect(r.x, r.y, r.w, r.h, sel ? blend565(70, C(t.accent), C(C_BTN)) : C(C_BTN));
        uint16_t edge = sel ? C(t.accent) : C(C_EDGE);
        cv.fillRect(r.x, r.y, r.w, 1, edge);
        cv.fillRect(r.x, r.y + r.h - 1, r.w, 1, edge);
        cv.fillRect(r.x, r.y, 1, r.h, edge);
        cv.fillRect(r.x + r.w - 1, r.y, 1, r.h, edge);
        if (sel) cv.fillRect(r.x + 1, r.y + r.h - 3, r.w - 2, 2, C(t.accent));
        cv.text(font_ui, r.x + 14, r.y + 22, kSkinNames[i], C(sel ? t.accentBright : C_TEXT), ALIGN_LEFT, 1);
        cv.text(i18n::font(font_small), r.x + 14, r.y + 40, i18n::T(en[i], th[i]), C(C_MUTED), ALIGN_LEFT, 1);
    }
}

void draw(const Settings &s) {
    Canvas &cv = gauge_ui::canvas();
    PushFn push = gauge_ui::pushFn();
    const uint16_t *bg = kThemes[s.theme].background;
    // 40-row strips: 320*40 fits the shared buffer; every element clips itself
    for (int y = 0; y < 240; y += 40) {
        cv.begin(0, y, 320, 40, bg);
        if (s_page == 1)      composeAlerts(cv, s);
        else if (s_page == 2) composeHonda(cv, s);
        else if (s_page == 3) composeTheme(cv, s);
        else if (s_page == 4) composeSkin(cv, s);
        else                  compose(cv, s);
        push(0, y, 320, 40, cv.px);
    }
}

void openMain() { s_page = 0; }     // called when the settings screen is (re)opened

// Taps on the ALERT sub-page: BACK, master on/off, three threshold steppers, reset to defaults.
static Action tapAlerts(int x, int y, Settings &s) {
    if (inside(R_DONE, x, y)) { s_page = 0; draw(s); return ACT_NONE; }   // BACK -> main page
    Action a = ACT_NONE;
    if (inside(RA_MASTER, x, y)) { s.alerts = !s.alerts; a = ACT_CHANGED; }
    struct { uint8_t *val; uint8_t lo, hi, step; } row[3] = {
        { &s.alertCoolant, Settings::kCoolMin, Settings::kCoolMax, Settings::kCoolStep },
        { &s.alertVoltLo,  Settings::kVLoMin,  Settings::kVLoMax,  Settings::kVLoStep },
        { &s.alertIat,     Settings::kIatMin,  Settings::kIatMax,  Settings::kIatStep },
    };
    for (int r = 0; r < 3; r++) {
        if (inside(alDn(r), x, y) && *row[r].val > row[r].lo) {
            int nv = *row[r].val - row[r].step; *row[r].val = nv < row[r].lo ? row[r].lo : nv; a = ACT_CHANGED;
        }
        if (inside(alUp(r), x, y) && *row[r].val < row[r].hi) {
            int nv = *row[r].val + row[r].step; *row[r].val = nv > row[r].hi ? row[r].hi : nv; a = ACT_CHANGED;
        }
    }
    if (inside(RA_DEFAULT, x, y)) {
        s.alertCoolant = COOLANT_CRIT; s.alertVoltLo = 118; s.alertIat = IAT_CRIT; a = ACT_CHANGED;
    }
    if (a == ACT_CHANGED) draw(s);
    return a;
}

// Taps on the HONDA model page: BACK, or pick a model (also sets its default shift point).
static Action tapHonda(int x, int y, Settings &s) {
    if (inside(R_DONE, x, y)) { s_page = 0; draw(s); return ACT_NONE; }
    Action a = ACT_NONE;
    for (int i = 0; i < HM_COUNT; i++)
        if (inside(hmCard(i), x, y) && s.hondaModel != i) {
            s.hondaModel = i;
            s.shiftRpm = (i == HM_GIORNO) ? 9000 : 8500;
            a = ACT_CHANGED;
        }
    if (a == ACT_CHANGED) draw(s);
    return a;
}

// Taps on the THEME page: BACK, or pick a colour theme.
static Action tapTheme(int x, int y, Settings &s) {
    if (inside(R_DONE, x, y)) { s_page = 0; draw(s); return ACT_NONE; }
    Action a = ACT_NONE;
    for (int i = 0; i < THEME_COUNT; i++)
        if (inside(themeCard(i), x, y) && s.theme != i) { s.theme = i; a = ACT_CHANGED; }
    if (a == ACT_CHANGED) draw(s);
    return a;
}

// Taps on the SKIN page: BACK, or pick a gauge layout.
static Action tapSkin(int x, int y, Settings &s) {
    if (inside(R_DONE, x, y)) { s_page = 0; draw(s); return ACT_NONE; }
    Action a = ACT_NONE;
    for (int i = 0; i < SKIN_COUNT; i++)
        if (inside(skinCard(i), x, y) && s.skin != i) { s.skin = (uint8_t)i; a = ACT_CHANGED; }
    if (a == ACT_CHANGED) draw(s);
    return a;
}

Action tap(int x, int y, Settings &s) {
    if (s_page == 1) return tapAlerts(x, y, s);
    if (s_page == 2) return tapHonda(x, y, s);
    if (s_page == 3) return tapTheme(x, y, s);
    if (s_page == 4) return tapSkin(x, y, s);
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
    if (inside(R_THEME, x, y)) {                                                 // open the theme page
        if (s.skin == SKIN_DEFAULT) { s_page = 3; draw(s); }                       // (DEFAULT skin only)
        return ACT_NONE;
    }
    if (inside(R_SKIN, x, y))  { s_page = 4; draw(s); return ACT_NONE; }        // open the skin page
    // The MODEL chip (or tapping HONDA again when it is already the source) opens the model page.
    // HONDA is the last shown source button (kShownSrc), so sourceBtn(kShownSrcN - 1).
    if (s.source == SRC_HONDA && (inside(R_HMODEL, x, y) ||
        inside(sourceBtn(kShownSrcN - 1), x, y))) { s_page = 2; draw(s); return ACT_NONE; }
    for (int j = 0; j < kShownSrcN; j++)
        if (inside(sourceBtn(j), x, y) && s.source != kShownSrc[j]) { s.source = kShownSrc[j]; a = ACT_CHANGED; }

    if (inside(R_SHIFT_DN, x, y) && s.shiftRpm > Settings::kShiftMin) { s.shiftRpm -= Settings::kShiftStep; a = ACT_CHANGED; }
    if (inside(R_SHIFT_UP, x, y) && s.shiftRpm < Settings::kShiftMax) { s.shiftRpm += Settings::kShiftStep; a = ACT_CHANGED; }
#if BACKLIGHT_DIMMING
    if (inside(R_BRI_DN, x, y) && s.brightness > Settings::kBrightMin) { s.brightness -= Settings::kBrightStep; a = ACT_CHANGED; }
    if (inside(R_BRI_UP, x, y) && s.brightness < 100) { s.brightness += Settings::kBrightStep; a = ACT_CHANGED; }
#endif
    if (inside(R_BEEP, x, y)) { s.beep = !s.beep; a = ACT_CHANGED; }
    if (inside(R_PANELS, x, y)) { s.panels = (s.panels + 1) % PANELS_COUNT; a = ACT_CHANGED; }
    if (inside(R_GEAR, x, y)) { s.gearMode = (s.gearMode + 1) % GEARMODE_COUNT; a = ACT_CHANGED; }
    if (inside(R_KUART, x, y)) { s.klineUart = s.klineUart == 0 ? 2 : 0; a = ACT_CHANGED; }
    if (inside(R_UNITS, x, y)) { s.units = s.units ? 0 : 1; a = ACT_CHANGED; }
    if (inside(R_LANG, x, y)) { s.lang = s.lang ? 0 : 1; a = ACT_CHANGED; }
    if (inside(R_ALERT, x, y)) { s_page = 1; draw(s); return ACT_NONE; }   // open the ALERT page
    if (a == ACT_CHANGED || wasCleared) draw(s);
    return a;
}

}  // namespace settings_ui
