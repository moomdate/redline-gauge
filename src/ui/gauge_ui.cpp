#include "ui/gauge_ui.h"
#include "ui/canvas.h"
#include "ui/shift_slots.h"
#include "config.h"
#include "ui/theme.h"
#include "ui/splash_ui.h"
#include "fonts/font_rpm.h"
#include "fonts/font_speed.h"
#include "fonts/font_value.h"
#include "fonts/font_label.h"
#include "fonts/font_small.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// ---- palette (from the HTML design) ------------------------------------------
#define C_WHITE      0xffffff
#define C_STATUS     0xd9e2e7
#define C_LABEL      0xaab7bf
#define C_BLUE       0x38bdff      // "cold" level; theme-independent on purpose
#define C_VALUE      0xffffff
#define C_WARN       0xffc400
#define C_CRIT       0xff2e2e
#define C_CRIT_DIM   0x7a2626
#define C_DIM        0x55636d
#define C_TRACK      0x1a232b
#define C_SEG_RED    0xff2424
#define C_SEG_SHIFT  0xff1f1f

// ---- layout (screen px, measured from the 320x240 design) -------------------
struct Rect { int x, y, w, h; };
static const Rect R_STATUS = { 12,   7, 200, 15 };
static const Rect R_BAR    = {  8,  27, 200, 22 };
static const Rect R_RPM_LBL = { 14, 62, 196, 15 };  // "ENGINE SPEED" + PEAK
static const Rect R_RPM     = { 14,  77, 196, 51 };  // big number + RPM unit
static const Rect R_SPEED  = { 18, 183, 174, 28 };
static const int  PANEL_TOP[3] = { 33, 102, 171 };
static Rect panelRect(int i) { return { 226, PANEL_TOP[i] - 4, 88, 44 }; }

static const Rect R_SETUP  = { 262, 221, 54, 17 };   // settings button, on the carbon strip

enum { RG_STATUS, RG_BAR, RG_RPM_LBL, RG_RPM, RG_SPEED, RG_P0, RG_P1, RG_P2, RG_SETUP, RG_COUNT };

static uint16_t  s_buf[14000];                 // fits the largest region (RPM 196x66)
static Canvas    cv(s_buf, sizeof(s_buf) / sizeof(s_buf[0]));
static const Theme *s_theme = &kThemes[0];
static const uint16_t *s_bg = nullptr;
static PushFn    s_push = nullptr;
static char      s_key[RG_COUNT][64];         // what each region last showed
static int       s_pushed = 0;
static const LinkStatus *s_kl = nullptr;      // ECU link of the source, nullptr = none
static uint32_t  s_klNow = 0;
static int       s_pillX = 0;                  // left edge of the K-line pill, 0 = none

static inline uint16_t C(uint32_t rgb) { return rgb565(rgb); }

static const char *s_regionKey(int rg);   // previous key of a region (before changed() updates it)

// Returns true if the region must be redrawn (its key changed).
static bool changed(int rg, const char *key) {
    if (strncmp(s_key[rg], key, sizeof(s_key[rg])) == 0) return false;
    strncpy(s_key[rg], key, sizeof(s_key[rg]) - 1);
    s_key[rg][sizeof(s_key[rg]) - 1] = 0;
    return true;
}

static bool beginRegion(const Rect &r) { return cv.begin(r.x, r.y, r.w, r.h, s_bg); }

// Compose only columns [x0, x1) of region r. Painters always draw the whole region in
// screen coordinates and the canvas clips, so a partial push is pixel-identical to a
// full one - it just moves fewer bytes over SPI (verified by a host test).
static bool beginColumns(const Rect &r, int x0, int x1) {
    if (x0 < r.x) x0 = r.x;
    if (x1 > r.x + r.w) x1 = r.x + r.w;
    if (x1 <= x0) return false;
    return cv.begin(x0, r.y, x1 - x0, r.h, s_bg);
}

// Partial-update memory per region (reset by invalidate()).
struct BarPrev { bool valid; float pos, peak; bool shift, lit; };
static BarPrev s_barPrev = {};
static char    s_rpmPrev[16] = "", s_speedPrev[8] = "";
static void flush() {
    s_push(cv.x0, cv.y0, cv.w, cv.h, cv.px);
    s_pushed++;
}

static void fmtThousands(char *out, size_t n, int v) {
    if (v >= 1000) snprintf(out, n, "%d,%03d", v / 1000, v % 1000);
    else snprintf(out, n, "%d", v);
}

static uint32_t levelColor(Level lv, uint32_t normal, bool blink) {
    switch (lv) {
        case LV_COLD: return C_BLUE;
        case LV_WARN: return C_WARN;
        case LV_CRIT: return blink ? C_CRIT : C_CRIT_DIM;
        default:      return normal;
    }
}

// ---- status bar ---------------------------------------------------------------
// Link pill: what the source's ECU link is doing, at a glance.
//   live      green dot  "ECU OK"
//   working   amber ring "LINKING" / the step, e.g. "BT PAIRING" (ring blinks)
//   no answer red cross  "NO ECU 2s"   (reason + seconds to the next try)
struct Pill { uint32_t color; int icon; char text[16], brief[16]; };   // icon: 0 dot, 1 ring, 2 cross

static Pill linkPill(const LinkStatus &k, uint32_t now, bool blink) {
    Pill p = { s_theme->good, 0, "ECU OK", "ECU OK" };
    if (k.state == LS_CONNECTING || k.state == LS_OFF) {
        // the step when it fits ("BT PAIRING", "ELM INIT" ...), else LINKING
        const char *step = k.step[0] && strlen(k.step) <= 10 && strcmp(k.step, "WAKE-UP") ? k.step : "LINKING";
        p = { s_theme->warning, blink ? 1 : 3, "", "LINKING" };
        snprintf(p.text, sizeof p.text, "%s", step);
    } else if (k.state == LS_RETRY) {
        int32_t left = (int32_t)(k.retryAt - now);
        int sec = left > 0 ? (left + 999) / 1000 : 0;
        // short words: the pill shares the bar with "REDLINE" and the source name
        const char *why = !strcmp(k.reason, "NO K-LINE") ? "NO LINE"
                        : !strcmp(k.reason, "K-LINE LOW") ? "LINE LOW"
                        : k.reason[0] ? k.reason : "NO ECU";
        p = { C_CRIT, 2, "", "" };
        snprintf(p.brief, sizeof p.brief, "%s", why);              // without the countdown
        if (sec > 0) snprintf(p.text, sizeof p.text, "%s %ds", why, sec);
        else snprintf(p.text, sizeof p.text, "%s", why);
    }
    return p;
}

static void drawPillIcon(int icon, float cx, float cy, uint16_t c) {
    if (icon == 0) { cv.fillCircle(cx, cy, 2.6f, c); return; }
    if (icon == 1) {                                   // ring
        cv.fillCircle(cx, cy, 3.2f, c);
        cv.fillCircle(cx, cy, 1.7f, C(0x101418));
        return;
    }
    if (icon == 3) return;                             // ring, blink-off phase
    for (int i = -3; i <= 3; i++) {                    // cross, 2 px strokes
        cv.blendPixel((int)cx + i, (int)cy + i, c, 255);
        cv.blendPixel((int)cx + i + 1, (int)cy + i, c, 140);
        cv.blendPixel((int)cx + i, (int)cy - i, c, 255);
        cv.blendPixel((int)cx + i + 1, (int)cy - i, c, 140);
    }
}

static void drawStatus(const GaugeView &v) {
    char key[64];
    uint32_t dot = v.link == Link::Live      ? s_theme->good
                 : v.link == Link::Simulated ? s_theme->accentBright : v.dotColor;
    bool official = splash_ui::authentic();
    Pill pill = {};
    if (s_kl) pill = linkPill(*s_kl, s_klNow, ((s_klNow / 350) & 1) == 0);
    snprintf(key, sizeof key, "%s|%s|%06x|%d|%d|%s|%d|%06x", v.mode, s_kl ? "" : v.right, (unsigned)dot,
             v.dotOn, official, pill.text, pill.icon, (unsigned)pill.color);
    if (!changed(RG_STATUS, key) || !beginRegion(R_STATUS)) return;

    if (v.dotOn && !s_kl) cv.fillCircle(21.5f, 15.5f, 2.3f, C(dot));
    if (official) cv.text(font_small, 28, 19, STATUS_TITLE, C(C_STATUS));
    else          cv.text(font_small, 28, 19, "UNOFFICIAL", C(C_CRIT));
    cv.text(font_small, 104, 19, v.mode, C(s_theme->accentBright));
    if (s_kl) {
        // rounded-off pill, right-aligned on the bar: tinted fill, 1 px rim, icon, text
        // never over the source name: drop the countdown when the pill would reach it
        int modeEnd = 104 + cv.textWidth(font_small, v.mode) + 6;
        const char *txt = 210 - (cv.textWidth(font_small, pill.text) + 20) < modeEnd ? pill.brief : pill.text;
        int tw = cv.textWidth(font_small, txt);
        int w = tw + 20, x = 210 - w, y = 8, h = 13;
        uint16_t c = C(pill.color);
        cv.blendRect(x + 1, y, w - 2, h, c, 48);
        cv.blendRect(x, y + 1, 1, h - 2, c, 48);
        cv.blendRect(x + w - 1, y + 1, 1, h - 2, c, 48);
        cv.blendRect(x + 1, y, w - 2, 1, c, 160);
        cv.blendRect(x + 1, y + h - 1, w - 2, 1, c, 110);
        drawPillIcon(pill.icon, x + 8.5f, y + 6.5f, c);
        cv.text(font_small, x + 15, 19, txt, C(pill.icon == 0 ? C_STATUS : pill.color));
        s_pillX = x;
    } else {
        s_pillX = 0;
        bool msg = v.right[0] && (v.right[0] < '0' || v.right[0] > '9');
        cv.text(font_small, 206, 19, v.right, C(msg ? dot : C_STATUS), ALIGN_RIGHT);
    }
    flush();
}

// ---- shift bar ------------------------------------------------------------------
static uint32_t slotColor(int s) {
    if (s >= SLOT_COUNT - 2) return C_SEG_RED;       // the two slots painted red in the art
    if (s >= SLOT_COUNT - 4) return s_theme->warning;
    return s_theme->accent;
}

static float rpmToPos(float rpm) {
    float p = rpm / RPM_MAX * SLOT_COUNT;
    return p < 0 ? 0 : p > SLOT_COUNT ? SLOT_COUNT : p;
}

// x of the lit edge for bar position p on slot row `row` (slots are slanted, so it differs per row)
static float barEdgeX(float p, int row) {
    if (p <= 0) return SLOT_SPAN[row][0][0];
    int s = (int)p;
    float f = p - s;
    if (s >= SLOT_COUNT) { s = SLOT_COUNT - 1; f = 1; }
    float a = SLOT_SPAN[row][s][0], b = SLOT_SPAN[row][s][1] + 1;
    return a + f * (b - a);
}

static void paintBar(float pos, float peak, bool shift, bool lit) {
    for (int r = 0; r < SLOT_ROWS; r++) {
        int y = SLOT_Y0 + r;
        float t = (float)r / (SLOT_ROWS - 1);
        // glossy vertical shading: bright lip on top, darker toward the bottom
        float sh = r == 0 ? 0.50f : r == 1 ? 0.25f : 0.10f - t * 0.22f;
        if (lit) {
            for (int s = 0; s < SLOT_COUNT; s++) {
                float f = pos - s;
                if (f <= 0) break;
                if (f > 1) f = 1;
                float a = SLOT_SPAN[r][s][0], b = SLOT_SPAN[r][s][1] + 1;
                uint16_t c = shade565(C(shift ? C_SEG_SHIFT : slotColor(s)), sh);
                cv.hspan(a, a + f * (b - a), y, c);
            }
        }
        if (peak > 0) {
            int s = (int)peak;
            if (s >= SLOT_COUNT) s = SLOT_COUNT - 1;
            float a = SLOT_SPAN[r][s][0], b = SLOT_SPAN[r][s][1] + 1;
            float x = a + (peak - s) * (b - a);
            if (x > b - 1) x = b - 1;
            cv.hspan(x - 0.5f, x + 1.5f, y, C(0xffffff));
        }
    }
}

static void drawBar(const GaugeView &v) {
    float pos  = v.rpmValid ? rpmToPos(v.rpmBar) : 0;
    float peak = v.peakMarker > 0 ? rpmToPos(v.peakMarker) : 0;
    bool lit   = !(v.shift && !v.shiftFlash);
    char key[64];
    // quantised to 1/4 px of a ~13 px slot -> ~50 steps per slot, invisible stepping
    snprintf(key, sizeof key, "%d|%d|%d|%d", (int)(pos * 52), (int)(peak * 52), v.shift, lit);
    if (!changed(RG_BAR, key)) return;

    // Only the columns between the old and new lit edge (and old / new peak marker) change.
    // A shift-light flash or the first draw repaints the whole bar.
    const BarPrev &o = s_barPrev;
    int x0 = R_BAR.x, x1 = R_BAR.x + R_BAR.w;
    if (o.valid && o.shift == v.shift && o.lit == lit) {
        float lo = 1e9f, hi = -1e9f;
        auto span = [&](float a, float b) { if (a < lo) lo = a; if (b > hi) hi = b; };
        const int rows[2] = { 0, SLOT_ROWS - 1 };         // top row leans right, bottom left
        if ((int)(pos * 52) != (int)(o.pos * 52))
            for (int r : rows) {
                float ea = barEdgeX(o.pos, r), eb = barEdgeX(pos, r);
                span(fminf(ea, eb) - 1, fmaxf(ea, eb) + 2);
            }
        const float peaks[2] = { o.peak, peak };
        for (float pk : peaks)
            if (pk > 0 && (int)(o.peak * 52) != (int)(peak * 52))
                for (int r : rows) { float x = barEdgeX(pk, r); span(x - 2, x + 3); }
        if (hi < lo) { s_barPrev = { true, pos, peak, v.shift, lit }; return; }
        x0 = (int)floorf(lo);
        x1 = (int)ceilf(hi);
    }
    s_barPrev = { true, pos, peak, v.shift, lit };
    if (!beginColumns(R_BAR, x0, x1)) return;
    paintBar(pos, peak, v.shift, lit);
    flush();
}

// ---- engine speed ------------------------------------------------------------------
static void drawRpm(const GaugeView &v) {
    char num[12], peak[20] = "";
    if (v.rpmValid) fmtThousands(num, sizeof num, v.rpmText);
    else strcpy(num, "--");
    // shown only once there is a peak above where the engine is now - right after a reset
    // (or while sitting at the peak) the label stays empty
    if (v.sessionPeak > 0 && v.sessionPeak >= v.rpmText + 200) {
        char p[12];
        fmtThousands(p, sizeof p, v.sessionPeak);
        snprintf(peak, sizeof peak, "PEAK %s", p);
    }
    uint32_t color = !v.rpmValid ? C_DIM : v.shift ? C_CRIT : C_WHITE;
    // no ECU link: say so where the eye goes first (the pill has the details)
    bool offline = s_kl && s_kl->state == LS_RETRY;
    if (offline) snprintf(peak, sizeof peak, "NOT CONNECTED");

    // both rows are painted by the same code, clipped to their own region
    auto paint = [&]() {
        cv.text(font_small, 19, 75, "ENGINE SPEED", C(C_LABEL), ALIGN_LEFT, 1);
        if (peak[0]) cv.text(font_small, 206, 75, peak, C(offline ? C_CRIT : 0x6f808b), ALIGN_RIGHT);
        int w = cv.text(font_rpm, 18, 118, num, C(color), ALIGN_LEFT, 0, true);
        cv.text(font_small, 18 + w + 8, 118, "RPM", C(s_theme->accentBright), ALIGN_LEFT, 1);
    };
    char key[64];
    snprintf(key, sizeof key, "%s|%d", peak, offline);
    if (changed(RG_RPM_LBL, key) && beginRegion(R_RPM_LBL)) { paint(); flush(); }

    snprintf(key, sizeof key, "%s|%06x", num, (unsigned)color);
    const char *prevSep = strchr(s_regionKey(RG_RPM), '|');
    bool sameColor = prevSep && strcmp(prevSep, strchr(key, '|')) == 0;   // compare before changed() overwrites
    if (!changed(RG_RPM, key)) return;
    // Digits are tabular, so when the length and colour are unchanged only the cells from the
    // first differing character onward need pushing (usually the last two or three digits).
    int x0 = R_RPM.x, x1 = R_RPM.x + R_RPM.w;
    size_t n = strlen(num);
    if (s_rpmPrev[0] && sameColor && strlen(s_rpmPrev) == n) {
        size_t i = 0;
        while (i < n && s_rpmPrev[i] == num[i]) i++;
        char prefix[16];
        memcpy(prefix, num, i);
        prefix[i] = 0;
        x0 = 18 + (i ? cv.textWidth(font_rpm, prefix, 0, true) : 0) - 2;
        x1 = 18 + cv.textWidth(font_rpm, num, 0, true) + 3;
    }
    snprintf(s_rpmPrev, sizeof s_rpmPrev, "%s", num);
    if (!beginColumns(R_RPM, x0, x1)) return;
    paint();
    flush();
}

// ---- vehicle speed + gear ------------------------------------------------------------
static void drawSpeed(const GaugeView &v) {
    char num[8], gear[4] = "";
    if (v.speedValid) snprintf(num, sizeof num, "%03d", v.speed);
    else strcpy(num, "---");
    if (v.gear == 0) strcpy(gear, "N");
    else if (v.gear > 0) snprintf(gear, sizeof gear, "%d", v.gear);
    char key[64];
    snprintf(key, sizeof key, "%s|%s", num, gear);
    const char *prevGear = strchr(s_regionKey(RG_SPEED), '|');
    bool sameGear = prevGear && strcmp(prevGear + 1, gear) == 0;
    if (!changed(RG_SPEED, key)) return;
    int x0 = R_SPEED.x, x1 = R_SPEED.x + R_SPEED.w;
    if (s_speedPrev[0] && sameGear && v.speedValid) {        // only digits changed: push those cells
        size_t i = 0;
        while (num[i] && s_speedPrev[i] == num[i]) i++;
        char prefix[8];
        memcpy(prefix, num, i);
        prefix[i] = 0;
        x0 = 68 + (i ? cv.textWidth(font_speed, prefix, 0, true) : 0) - 2;
        x1 = 68 + cv.textWidth(font_speed, num, 0, true) + 3;
    }
    snprintf(s_speedPrev, sizeof s_speedPrev, "%s", v.speedValid ? num : "");
    if (!beginColumns(R_SPEED, x0, x1)) return;

    cv.text(font_label, 23, 203, "SPEED", C(s_theme->accentBright), ALIGN_LEFT, 1);
    int w = cv.text(font_speed, 68, 207, num, C(v.speedValid ? C_WHITE : C_DIM), ALIGN_LEFT, 0, true);
    cv.text(font_small, 68 + w + 3, 207, "km/h", C(0xbac6cc));
    if (gear[0]) {
        cv.text(font_small, 147, 207, "GEAR", C(s_theme->warning));
        cv.text(font_speed, 178, 207, gear, C(s_theme->warning), ALIGN_CENTER);
    }
    flush();
}

// ---- side panels -----------------------------------------------------------------------
static void drawPanel(int i, const char *label, bool valid, const char *value, const char *unit,
                      uint32_t color, float frac) {
    int top = PANEL_TOP[i];
    int barW = 76;
    int fill = valid ? (int)lroundf((frac < 0 ? 0 : frac > 1 ? 1 : frac) * barW) : -1;
    char key[64];
    snprintf(key, sizeof key, "%s|%d|%s|%06x|%d", label, valid, value, (unsigned)color, fill);
    if (!changed(RG_P0 + i, key) || !beginRegion(panelRect(i))) return;

    cv.text(font_label, 230, top + 8, label, C(s_theme->accentBright), ALIGN_LEFT, 1);
    int w = cv.text(font_value, 230, top + 29, valid ? value : "--", C(valid ? color : C_DIM),
                    ALIGN_LEFT, 0, true);
    cv.text(font_small, 230 + w + 3, top + 29, unit, C(0xd1d9de));

    cv.fillRect(230, top + 34, barW, 3, C(C_TRACK));
    if (fill > 0) {
        cv.fillRect(230, top + 34, fill, 3, C(color));
        cv.fillRect(230, top + 34, fill, 1, shade565(C(color), 0.35f));
    }
    flush();
}

// ---- settings button ------------------------------------------------------------------
// Small cog: 8 teeth, drawn with 4x4 supersampling so it stays crisp at 13 px.
static void drawCog(float cx, float cy, uint16_t c) {
    for (int y = (int)cy - 7; y <= (int)cy + 7; y++)
        for (int x = (int)cx - 7; x <= (int)cx + 7; x++) {
            int hits = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float dx = x + (sx + 0.5f) / 4 - cx, dy = y + (sy + 0.5f) / 4 - cy;
                    float r = sqrtf(dx * dx + dy * dy), a = atan2f(dy, dx);
                    float outer = (cosf(a * 8) > 0.1f) ? 6.3f : 4.6f;   // teeth
                    if (r <= outer && r >= 2.0f) hits++;
                }
            if (hits) cv.blendPixel(x, y, c, (uint8_t)(hits * 255 / 16));
        }
}

static void drawSetup() {
    char key[16];
    snprintf(key, sizeof key, "%s", s_theme->key);
    if (!changed(RG_SETUP, key) || !beginRegion(R_SETUP)) return;
    const Rect &r = R_SETUP;
    cv.blendRect(r.x, r.y, r.w, r.h, 0x0000, 190);
    uint16_t a = C(s_theme->accent);
    cv.fillRect(r.x, r.y, r.w, 1, a);
    cv.fillRect(r.x, r.y + r.h - 1, r.w, 1, shade565(a, -0.5f));
    cv.fillRect(r.x, r.y, 1, r.h, shade565(a, -0.3f));
    cv.fillRect(r.x + r.w - 1, r.y, 1, r.h, shade565(a, -0.3f));
    drawCog(r.x + 10, r.y + 8.5f, C(s_theme->accentBright));
    cv.text(font_small, r.x + 20, r.y + 12, "SETUP", C(C_STATUS));
    flush();
}

static const char *s_regionKey(int rg) { return s_key[rg]; }

// ---- selectable panel values ------------------------------------------------------------------
static uint8_t s_items[3] = { PI_COOLANT, PI_VOLTAGE, PI_INTAKE };

static bool itemValid(uint8_t item, const GaugeView &v) {
    switch (item) {
        case PI_COOLANT:  return v.coolValid;
        case PI_VOLTAGE:  return v.voltValid;
        case PI_INTAKE:   return v.iatValid;
        case PI_AFR:      return v.afrValid;
        case PI_THROTTLE: return v.tpsValid;
        case PI_FUEL_LEVEL: return v.fuelLevelValid;
        case PI_FUEL_RATE:  return v.fuelRateValid;
        case PI_STFT:       return v.stftValid;
        case PI_LTFT:       return v.ltftValid;
        case PI_MAP:        return v.mapValid;
        case PI_IGNITION:   return v.ignitionValid;
        case PI_O2_MV:      return v.o2MvValid;
        case PI_LOAD:       return v.loadValid;
        default:          return false;
    }
}

static void drawItem(int i, uint8_t item, const GaugeView &v) {
    char buf[12];
    switch (item) {
    case PI_VOLTAGE:
        snprintf(buf, sizeof buf, "%.1f", v.volt);
        drawPanel(i, PANEL2_LABEL, v.voltValid, buf, "V", levelColor(v.voltLvl, s_theme->good, v.blink),
                  (v.volt - VOLT_BAR_MIN) / (VOLT_BAR_MAX - VOLT_BAR_MIN));
        break;
    case PI_LOAD:
        snprintf(buf, sizeof buf, "%d", v.load);
        drawPanel(i, "LOAD", v.loadValid, buf, "%", s_theme->accentBright, v.load / 100.0f);
        break;
    case PI_INTAKE:
        snprintf(buf, sizeof buf, "%d", v.iat);
        drawPanel(i, PANEL3_LABEL, v.iatValid, buf, "°C", levelColor(v.iatLvl, C_VALUE, v.blink),
                  (v.iat - IAT_BAR_MIN) / (float)(IAT_BAR_MAX - IAT_BAR_MIN));
        break;
    case PI_AFR: {
        // narrowband estimate: the word says what it means, the number is a guide
        snprintf(buf, sizeof buf, "%.1f", v.afr);
        const char *unit = v.afrLvl == LV_WARN ? "RICH" : v.afrLvl == LV_COLD ? "LEAN" : "OK";
        drawPanel(i, "AFR", v.afrValid, buf, v.afrValid ? unit : "", levelColor(v.afrLvl, s_theme->good, v.blink),
                  (v.afr - AFR_BAR_MIN) / (AFR_BAR_MAX - AFR_BAR_MIN));
        break;
    }
    case PI_THROTTLE:
        snprintf(buf, sizeof buf, "%d", v.tps);
        drawPanel(i, "THROTTLE", v.tpsValid, buf, "%", s_theme->accentBright, v.tps / 100.0f);
        break;
    case PI_FUEL_LEVEL:
        snprintf(buf, sizeof buf, "%d", v.fuelLevel);
        drawPanel(i, "FUEL", v.fuelLevelValid, buf, "%", s_theme->good, v.fuelLevel / 100.0f);
        break;
    case PI_FUEL_RATE:
        snprintf(buf, sizeof buf, "%.1f", v.fuelRate);
        drawPanel(i, "FUEL RATE", v.fuelRateValid, buf, "L/h", s_theme->accentBright, v.fuelRate / 100.0f);
        break;
    case PI_STFT:
        snprintf(buf, sizeof buf, "%.1f", v.stft);
        drawPanel(i, "STFT", v.stftValid, buf, "%", s_theme->accentBright, (v.stft + 25.0f) / 50.0f);
        break;
    case PI_LTFT:
        snprintf(buf, sizeof buf, "%.1f", v.ltft);
        drawPanel(i, "LTFT", v.ltftValid, buf, "%", s_theme->accentBright, (v.ltft + 25.0f) / 50.0f);
        break;
    case PI_MAP:
        snprintf(buf, sizeof buf, "%d", v.map);
        drawPanel(i, "MAP", v.mapValid, buf, "KPA", s_theme->accentBright, v.map / 255.0f);
        break;
    case PI_IGNITION:
        snprintf(buf, sizeof buf, "%.1f", v.ignition);
        drawPanel(i, "IGN ADV", v.ignitionValid, buf, "DEG", s_theme->accentBright, v.ignition / 64.0f);
        break;
    case PI_O2_MV:
        snprintf(buf, sizeof buf, "%d", v.o2Mv);
        drawPanel(i, "O2 SENSOR", v.o2MvValid, buf, "mV", s_theme->accentBright, v.o2Mv / 1000.0f);
        break;
    default:
        snprintf(buf, sizeof buf, "%d", v.coolant);
        drawPanel(i, PANEL1_LABEL, v.coolValid, buf, "°C", levelColor(v.coolLvl, C_VALUE, v.blink),
                  (v.coolant - COOLANT_BAR_MIN) / (float)(COOLANT_BAR_MAX - COOLANT_BAR_MIN));
        break;
    }
}

// ---- public -------------------------------------------------------------------------------
namespace gauge_ui {

void begin(PushFn push, const Theme &theme) {
    s_push = push;
    setTheme(theme);
}

void setTheme(const Theme &theme) {
    s_theme = &theme;
    s_bg = theme.background;
    redrawAll();
}

const Theme &theme() { return *s_theme; }

void redrawAll() {
    const int rows = (int)(sizeof(s_buf) / sizeof(s_buf[0])) / 320;
    for (int y = 0; y < 240; y += rows) {
        int h = y + rows > 240 ? 240 - y : rows;
        cv.begin(0, y, 320, h, s_bg);
        s_push(0, y, 320, h, cv.px);
    }
    invalidate();
}

void invalidate() {
    memset(s_key, 0, sizeof s_key);
    for (auto &k : s_key) k[0] = 1;
    s_barPrev.valid = false;                       // next frame: full repaint of every region
    s_rpmPrev[0] = s_speedPrev[0] = 0;
}

void render(const GaugeView &v) {
    s_pushed = 0;
    drawBar(v);
    drawRpm(v);
    drawSpeed(v);
    drawStatus(v);
    drawSetup();

    if (v.hybrid) {
        drawItem(0, s_items[0], v);
        char buf[12];
        snprintf(buf, sizeof buf, "%d", v.soc);
        drawPanel(1, HYBRID_PANEL2_LABEL, v.socValid, buf, "%", levelColor(v.socLvl, s_theme->good, v.blink),
                  v.soc / 100.0f);
        float kw = v.kw;
        if (fabsf(kw) < 0.05f) kw = 0;                 // no "-0.0"
        snprintf(buf, sizeof buf, fabsf(kw) >= 100 ? "%.0f" : "%.1f", kw);
        // regen (negative) in the theme's "good" colour, drive power in white
        drawPanel(2, HYBRID_PANEL3_LABEL, v.kwValid, buf, "kW", kw < 0 ? s_theme->good : C_VALUE,
                  fabsf(kw) / HVKW_BAR_MAX);
        return;
    }
    for (int i = 0; i < 3; i++) drawItem(i, s_items[i], v);
}

void setLink(const LinkStatus *st, uint32_t now) {
    s_kl = st;
    s_klNow = now;
}

int lastPushedRegions() { return s_pushed; }
Canvas &canvas() { return cv; }
PushFn pushFn() { return s_push; }

bool hitStatus(int x, int y) { return y < 26 && x < 215; }
void setPanelItems(const uint8_t items[3]) {
    for (int i = 0; i < 3; i++) s_items[i] = items[i] < PI_COUNT ? items[i] : PI_COOLANT;
}

int hitPanel(int x, int y, const GaugeView &v) {
    if (x < 222 || y >= 209) return -1;                // the SETUP button sits just below
    for (int i = 0; i < 3; i++) {
        Rect r = panelRect(i);
        if (y >= r.y - 2 && y < r.y + r.h + 2) return v.hybrid && i > 0 ? -1 : i;
    }
    return -1;
}

uint8_t nextPanelItem(int i, const GaugeView &v) {
    bool any = false;
    for (int k = 0; k < PI_COUNT; k++) any |= itemValid(k, v);
    for (int step = 1; step < PI_COUNT; step++) {
        uint8_t c = (s_items[i] + step) % PI_COUNT;
        bool shown = false;
        for (int j = 0; j < 3; j++) shown |= j != i && s_items[j] == c && !(v.hybrid && j > 0);
        if (!shown && (!any || itemValid(c, v))) return c;
    }
    return s_items[i];
}

bool hitLink(int x, int y)   { return s_pillX && y < 30 && x >= s_pillX - 8 && x < 218; }
bool hitSetup(int x, int y)  { return x >= 248 && y >= 210; }
bool hitMain(int x, int y)   { return x < 215 && y >= 26; }

}  // namespace gauge_ui
