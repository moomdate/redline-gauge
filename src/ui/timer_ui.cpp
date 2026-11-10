#include "ui/timer_ui.h"
#include "ui/canvas.h"
#include "ui/gauge_ui.h"
#include "ui/shift_slots.h"
#include "ui/splash_ui.h"
#include "ui/theme.h"
#include "config.h"
#include "fonts/font_timer.h"
#include "fonts/font_ready.h"
#include "fonts/font_speed.h"
#include "fonts/font_value.h"
#include "fonts/font_label.h"
#include "fonts/font_small.h"
#include "fonts/font_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui/gauge_model.h"

static uint8_t s_units = 0;

namespace timer_ui {

void setUnits(uint8_t units) { uint8_t u = units ? 1 : 0; if (u != s_units) { s_units = u; invalidate(); } }

// Same geometry as gauge_ui.cpp (it is baked into the background art).
struct Rect { int x, y, w, h; };
static const Rect R_STATUS  = { 12,   7, 200, 15 };
static const Rect R_BAR     = {  8,  27, 200, 22 };
static const Rect R_BIG_LBL = { 14,  62, 196, 15 };
static const Rect R_BIG     = { 14,  77, 196, 51 };
static const Rect R_SPEED   = { 18, 183, 174, 28 };   // bottom row: the clock
static const Rect R_Z200    = { 14, 132, 196, 46 };   // 0-200 box under the speed
static const int  PANEL_TOP[3] = { 33, 102, 171 };
static Rect panelRect(int i) { return { 226, PANEL_TOP[i] - 4, 88, 44 }; }
static const Rect R_LOGBTN  = { 204, 221, 54, 17 };
static const Rect R_BTN     = { 262, 221, 54, 17 };

// log page
static const Rect L_BACK  = { 250,   4,  64, 22 };
static const Rect L_CLEAR = {   8, 212, 150, 24 };
static const Rect L_NEW   = { 164, 212, 148, 24 };

enum { RG_STATUS, RG_BAR, RG_LBL, RG_BIG, RG_SPEED, RG_P0, RG_P1, RG_P2, RG_Z200, RG_LOGBTN, RG_BTN, RG_COUNT };
static char s_key[RG_COUNT][72];

// Partial-push memory (what is on the panel right now), reset by invalidate().
static char  s_bigPrev[8] = "";                  // speed digits currently shown
static char  s_timePrev[12] = "";                // clock digits currently shown
static struct { bool valid; float pos; uint32_t color; int staged; } s_barPrev = {};
static int   s_fillPrev[4] = { -1, -1, -1, -1 };  // panel 0..2 + the 0-200 box: bar fill px

#define C_STATUS 0xd9e2e7
#define C_LABEL  0xaab7bf
#define C_DIM    0x55636d
#define C_MUTED  0x8796a0
#define C_TRACK  0x1a232b
#define C_EDGE   0x34434e

static const float kFlashS = 0.7f;               // a freshly stamped time shows amber this long

static inline uint16_t C(uint32_t rgb) { return rgb565(rgb); }
static Canvas &cv() { return gauge_ui::canvas(); }
static const Theme &th() { return gauge_ui::theme(); }

static bool inside(const Rect &r, int x, int y) {
    return x >= r.x - 3 && x < r.x + r.w + 3 && y >= r.y - 3 && y < r.y + r.h + 3;
}

static bool changed(int rg, const char *key) {
    if (!strncmp(s_key[rg], key, sizeof s_key[rg])) return false;
    snprintf(s_key[rg], sizeof s_key[rg], "%s", key);
    return true;
}
static bool begin(const Rect &r) { return cv().begin(r.x, r.y, r.w, r.h, th().background); }
// Compose only columns [x0, x1) of a region: painters draw in screen coordinates and the
// canvas clips, so a partial push is pixel-identical to a full one - it just moves fewer
// bytes over SPI (less tearing on the panel, and the frame stays short).
static bool beginColumns(const Rect &r, int x0, int x1) {
    if (x0 < r.x) x0 = r.x;
    if (x1 > r.x + r.w) x1 = r.x + r.w;
    if (x1 <= x0) return false;
    return cv().begin(x0, r.y, x1 - x0, r.h, th().background);
}
static void flush() { Canvas &c = cv(); gauge_ui::pushFn()(c.x0, c.y0, c.w, c.h, c.px); }

void invalidate() {
    for (auto &k : s_key) { k[0] = 1; k[1] = 0; }
    s_bigPrev[0] = s_timePrev[0] = 0;
    s_barPrev.valid = false;
    for (int &f : s_fillPrev) f = -1;
}

// Index of the first character that differs between what is shown and what is wanted, or
// -1 when the whole thing must be repainted (different length / nothing shown yet).
static int firstDiff(const char *prev, const char *next) {
    size_t n = strlen(next);
    if (!prev[0] || strlen(prev) != n) return -1;
    size_t i = 0;
    while (i < n && prev[i] == next[i]) i++;
    return (int)i;
}

// "12.34" or "--.--"
static void fmtSec(char *out, size_t n, float s) {
    if (s < 0) snprintf(out, n, "--.--");
    else snprintf(out, n, "%.2f", s > 99.99f ? 99.99f : s);
}
static void fmtCs(char *out, size_t n, uint16_t cs) {
    if (cs == RUN_NONE) snprintf(out, n, "--");
    else snprintf(out, n, "%u.%02u", cs / 100, cs % 100);
}

// ---- pieces ----------------------------------------------------------------------------
static void drawStatus(const char *state, uint32_t color) {
    char key[72];
    bool official = splash_ui::authentic();
    snprintf(key, sizeof key, "%s|%06x|%d", state, (unsigned)color, official);
    if (!changed(RG_STATUS, key) || !begin(R_STATUS)) return;
    Canvas &c = cv();
    c.fillCircle(21.5f, 15.5f, 2.3f, C(color));
    c.text(font_small, 28, 19, official ? STATUS_TITLE : "UNOFFICIAL", C(official ? C_STATUS : 0xff2e2e));
    c.text(font_small, 104, 19, "DRAG TIMER", C(th().accentBright));
    c.text(font_small, 206, 19, state, C(color), ALIGN_RIGHT);
    flush();
}

// x of the lit edge for bar position p on slot row `row` (slots are slanted)
static float barEdgeX(float p, int row) {
    if (p <= 0) return SLOT_SPAN[row][0][0];
    int s = (int)p;
    float f = p - s;
    if (s >= SLOT_COUNT) { s = SLOT_COUNT - 1; f = 1; }
    float a = SLOT_SPAN[row][s][0], b = SLOT_SPAN[row][s][1] + 1;
    return a + f * (b - a);
}

// The gauge's 13 slanted slots: `pos` of 13 lit in `color`, or `staged` amber lights.
// Only the columns between the old and the new lit edge are pushed when just `pos` moved.
static void drawBar(float pos, uint32_t color, int staged) {
    char key[72];
    int q = (int)(pos * 52);                       // ~50 steps per slot: stepping is invisible
    snprintf(key, sizeof key, "%d|%06x|%d", q, (unsigned)color, staged);
    if (!changed(RG_BAR, key)) return;
    int x0 = R_BAR.x, x1 = R_BAR.x + R_BAR.w;
    if (s_barPrev.valid && s_barPrev.color == color && s_barPrev.staged == staged) {
        float lo = 1e9f, hi = -1e9f;
        const int rows[2] = { 0, SLOT_ROWS - 1 };  // top row leans right, bottom row left
        for (int r : rows) {
            float ea = barEdgeX(s_barPrev.pos, r), eb = barEdgeX(pos, r);
            lo = fminf(lo, fminf(ea, eb) - 1);
            hi = fmaxf(hi, fmaxf(ea, eb) + 2);
        }
        x0 = (int)floorf(lo);
        x1 = (int)ceilf(hi);
    }
    s_barPrev = { true, pos, color, staged };
    if (!beginColumns(R_BAR, x0, x1)) return;
    Canvas &c = cv();
    for (int r = 0; r < SLOT_ROWS; r++) {
        int y = SLOT_Y0 + r;
        float t = (float)r / (SLOT_ROWS - 1);
        float sh = r == 0 ? 0.50f : r == 1 ? 0.25f : 0.10f - t * 0.22f;
        for (int s = 0; s < SLOT_COUNT; s++) {
            float a = SLOT_SPAN[r][s][0], b = SLOT_SPAN[r][s][1] + 1;
            if (s < staged) { c.hspan(a, b, y, shade565(C(th().warning), sh)); continue; }
            float f = pos - s;
            if (f <= 0) continue;
            if (f > 1) f = 1;
            c.hspan(a, a + f * (b - a), y, shade565(C(color), sh));
        }
    }
    flush();
}

// Label row (R_BIG_LBL) + the big number (R_BIG). The digits are tabular, so when only
// some of them changed just those cells are pushed (same trick as the gauge's RPM).
static void drawBig(const char *label, const char *right, const char *tag, const char *num, uint32_t color,
                    const char *unit) {
    auto paint = [&]() {
        Canvas &c = cv();
        c.text(font_small, 19, 75, label, C(C_LABEL), ALIGN_LEFT, 1);
        if (tag[0]) {
            int tw = c.textWidth(font_small, tag, 1) + 10;
            c.fillRect(206 - tw, 65, tw, 13, C(th().good));
            c.text(font_small, 206 - tw / 2, 75, tag, C(0x05140a), ALIGN_CENTER, 1);
        } else if (right[0]) {
            c.text(font_small, 206, 75, right, C(0x6f808b), ALIGN_RIGHT);
        }
        if (!num[0]) return;
        int w = c.text(font_timer, 18, 118, num, C(color), ALIGN_LEFT, 0, true);
        c.text(font_small, 18 + w + 6, 118, unit, C(th().accentBright), ALIGN_LEFT, 1);
    };
    char key[72];
    snprintf(key, sizeof key, "%s|%s|%s", label, right, tag);
    if (changed(RG_LBL, key) && begin(R_BIG_LBL)) { paint(); flush(); }
    if (!num[0]) return;                               // READY!! owns the big region
    snprintf(key, sizeof key, "%s|%06x|%s", num, (unsigned)color, unit);
    const char *prevSep = strchr(s_key[RG_BIG], '|');
    bool sameStyle = prevSep && strcmp(prevSep, strchr(key, '|')) == 0;   // colour + unit unchanged
    if (!changed(RG_BIG, key)) return;
    int x0 = R_BIG.x, x1 = R_BIG.x + R_BIG.w;
    int d = sameStyle ? firstDiff(s_bigPrev, num) : -1;
    if (d >= 0) {
        char prefix[8];
        memcpy(prefix, num, d);
        prefix[d] = 0;
        x0 = 18 + (d ? cv().textWidth(font_timer, prefix, 0, true) : 0) - 2;
        x1 = 18 + cv().textWidth(font_timer, num, 0, true) + 3;   // the unit sits past x1: unchanged
    }
    snprintf(s_bigPrev, sizeof s_bigPrev, "%s", num);
    if (!beginColumns(R_BIG, x0, x1)) return;
    paint();
    flush();
}

// READY: big flashing "READY!!" where the speed goes once the car moves.
static void drawReady(bool on) {
    char key[72];
    snprintf(key, sizeof key, "READY|%d", on);
    if (!changed(RG_BIG, key) || !begin(R_BIG)) return;
    s_bigPrev[0] = 0;                                  // the next speed frame repaints fully
    if (on) cv().text(font_ready, 112, 118, "READY!!", C(th().warning), ALIGN_CENTER, 1);
    flush();
}

// Bottom row: the clock (running time, or the result). Runs at the frame rate while timing,
// so only the digit cells that changed are pushed (usually the last one or two).
static void drawTime(const char *label, const char *value, uint32_t color) {
    char key[72];
    snprintf(key, sizeof key, "%s|%06x|%s", label, (unsigned)color, value);
    const char *prevSep = strrchr(s_key[RG_SPEED], '|');
    bool sameStyle = prevSep && !strncmp(s_key[RG_SPEED], key, prevSep - s_key[RG_SPEED] + 1);
    if (!changed(RG_SPEED, key)) return;
    int x0 = R_SPEED.x, x1 = R_SPEED.x + R_SPEED.w;
    int d = sameStyle ? firstDiff(s_timePrev, value) : -1;
    if (d >= 0) {
        char prefix[12];
        memcpy(prefix, value, d);
        prefix[d] = 0;
        x0 = 78 + (d ? cv().textWidth(font_value, prefix, 0, true) : 0) - 2;
        x1 = 78 + cv().textWidth(font_value, value, 0, true) + 3;
    }
    snprintf(s_timePrev, sizeof s_timePrev, "%s", value);
    if (!beginColumns(R_SPEED, x0, x1)) return;
    Canvas &c = cv();
    c.text(font_label, 23, 203, label, C(th().accentBright), ALIGN_LEFT, 1);
    int w = c.text(font_value, 78, 207, value, C(color), ALIGN_LEFT, 0, true);
    c.text(font_small, 78 + w + 3, 207, "SEC", C(0xbac6cc));
    flush();
}

// Split box states: waiting (dim), being chased (accent, bar fills with speed), just
// stamped (amber flash), stamped (white), stamped and a new best (green + BEST).
enum PanelState { PS_PENDING, PS_ACTIVE, PS_FRESH, PS_DONE, PS_BEST };

static uint32_t valueColor(PanelState ps) {
    switch (ps) {
        case PS_PENDING: return C_DIM;
        case PS_ACTIVE:  return th().accentBright;
        case PS_FRESH:   return th().warning;
        case PS_BEST:    return th().good;
        default:         return 0xffffff;
    }
}

// One split box. `slot` 0..2 = the right-hand panels, 3 = the wide 0-200 box. When only the
// progress bar moved, just its 3-px strip is pushed.
static void drawBox(int slot, const char *label, const char *value, PanelState ps, float prog) {
    bool wide = slot == 3;
    int lx = wide ? 19 : 230;                          // left edge of the text
    int ly = wide ? 145 : PANEL_TOP[slot] + 8;         // label baseline
    int vy = wide ? 168 : PANEL_TOP[slot] + 29;        // value baseline
    int bw = wide ? 180 : 76;                          // bar width
    int by = wide ? 172 : PANEL_TOP[slot] + 34;
    int rx = wide ? 199 : 312;                         // right edge (BEST tag)
    Rect region = wide ? R_Z200 : panelRect(slot);
    int rg = wide ? RG_Z200 : RG_P0 + slot;
    int fill = (int)lroundf((prog < 0 ? 0 : prog > 1 ? 1 : prog) * bw);
    char key[72];
    snprintf(key, sizeof key, "%s|%s|%d|%d", label, value, ps, fill);
    const char *prevSep = strrchr(s_key[rg], '|');
    bool sameText = prevSep && !strncmp(s_key[rg], key, prevSep - s_key[rg] + 1);
    if (!changed(rg, key)) return;
    bool stripOnly = sameText && s_fillPrev[slot] >= 0;
    s_fillPrev[slot] = fill;
    bool ok = stripOnly ? cv().begin(lx, by, bw, 3, th().background) : begin(region);
    if (!ok) return;
    Canvas &c = cv();
    if (!stripOnly) {
        c.text(font_label, lx, ly, label, C(ps == PS_PENDING ? 0x6f808b : th().accentBright), ALIGN_LEFT, 1);
        int w = c.text(font_value, lx, vy, value, C(valueColor(ps)), ALIGN_LEFT, 0, true);
        c.text(font_small, lx + w + 3, vy, "s", C(0xd1d9de));
        if (ps == PS_BEST) c.text(font_small, rx, vy, "BEST", C(th().good), ALIGN_RIGHT);
    }
    c.fillRect(lx, by, bw, 3, C(C_TRACK));
    if (fill > 0) c.fillRect(lx, by, fill, 3, C(ps >= PS_FRESH ? th().good : th().accent));
    flush();
}

static void smallButton(int rg, const Rect &r, const char *label, bool show) {
    char key[72];
    snprintf(key, sizeof key, "%s|%d", label, show);
    if (!changed(rg, key) || !begin(r)) return;
    Canvas &c = cv();
    if (show) {
        c.blendRect(r.x, r.y, r.w, r.h, 0x0000, 190);
        uint16_t a = C(th().accent);
        c.fillRect(r.x, r.y, r.w, 1, a);
        c.fillRect(r.x, r.y + r.h - 1, r.w, 1, shade565(a, -0.5f));
        c.fillRect(r.x, r.y, 1, r.h, shade565(a, -0.3f));
        c.fillRect(r.x + r.w - 1, r.y, 1, r.h, shade565(a, -0.3f));
        c.text(font_small, r.x + r.w / 2, r.y + 12, label, C(C_STATUS), ALIGN_CENTER);
    }
    flush();
}

// ---- timer screen ------------------------------------------------------------------------
void render(const DragTimer &t, const RunLog &log, uint32_t now, bool speedValid) {
    const Theme &T = th();
    TimerState st = t.state();
    char num[12], right[24] = "", label[32];
    const char *stateText = "";
    uint32_t stateColor = T.accentBright;
    const char *tag = "";

    switch (st) {
        case TS_NO_SPEED: stateText = "NO SPEED"; stateColor = 0xff4741; break;
        case TS_MOVING:   stateText = "STOP TO ARM"; stateColor = T.warning; break;
        case TS_STAGED:   stateText = "READY"; stateColor = T.warning; break;
        case TS_RUN:      stateText = "RUN"; stateColor = T.good; break;
        case TS_FINISH:   stateText = "FINISH"; stateColor = T.good; break;
        case TS_SAVED:    stateText = "SAVED"; stateColor = T.good; break;
        case TS_NO_RESULT:stateText = "TOO SHORT"; stateColor = T.warning; break;
    }
    drawStatus(stateText, stateColor);

    // big number = live speed; bottom row = the clock
    char tnum[12], tlabel[16] = "TIME";
    uint32_t tColor = C_DIM;
    fmtSec(tnum, sizeof tnum, 0);
    int kmh = speedValid ? (int)lroundf(t.speed()) : -1;
    if (kmh > 999) kmh = 999;
    if (kmh >= 0) snprintf(num, sizeof num, "%d", displaySpeed(kmh, s_units)); else snprintf(num, sizeof num, "--");
    snprintf(label, sizeof label, "SPEED");
    if (st == TS_RUN) {
        fmtSec(tnum, sizeof tnum, t.elapsed(now));
        tColor = T.accentBright;
        int next = 0;
        while (next < DragTimer::kThresholds - 1 && t.speed() >= DragTimer::kTh[next]) next++;
        snprintf(right, sizeof right, "NEXT %d KM/H", (int)DragTimer::kTh[next]);
    } else if (st == TS_FINISH) {
        fmtSec(tnum, sizeof tnum, t.segTime(SEG_0_200, now));
        snprintf(tlabel, sizeof tlabel, "0-200");
        tColor = T.good;
        if (t.newBest(SEG_0_200)) tag = "NEW BEST";
        else snprintf(right, sizeof right, "FINISH");
    } else if (st == TS_SAVED) {                         // didn't reach 200: time to its top speed
        fmtSec(tnum, sizeof tnum, t.toMax());
        snprintf(tlabel, sizeof tlabel, "0-%d", (int)lroundf(t.maxKmh()));
        tColor = 0xffffff;
        snprintf(right, sizeof right, "TOP %d %s", displaySpeed((int)lroundf(t.maxKmh()), s_units), s_units ? "MPH" : "KM/H");
    } else if (st == TS_NO_RESULT) {
        fmtSec(tnum, sizeof tnum, -1);
        snprintf(right, sizeof right, "UNDER 30 - NOT LOGGED");
    } else {
        snprintf(label, sizeof label, st == TS_STAGED ? "LAUNCH TO START"
                                    : st == TS_MOVING ? "STOP TO ARM" : "NO SPEED YET");
        uint16_t b200 = log.best(SEG_0_200), b100 = log.best(SEG_0_100);
        if (b200 != RUN_NONE) snprintf(right, sizeof right, "BEST 0-200 %u.%02u", b200 / 100, b200 % 100);
        else if (b100 != RUN_NONE) snprintf(right, sizeof right, "BEST 0-100 %u.%02u", b100 / 100, b100 % 100);
    }
    if (st == TS_STAGED) {
        drawBig(label, right, tag, "", 0, "");                 // label row only
        drawReady(((now / 400) & 1) == 0);
    } else {
        drawBig(label, right, tag, num, kmh >= 0 ? 0xffffff : C_DIM, s_units ? "MPH" : "KM/H");
    }
    drawTime(tlabel, tnum, tColor);

    // bar: staging lights, live speed toward 200, or the result
    if (st == TS_STAGED) drawBar(0, T.accent, 3);
    else if (st == TS_FINISH) drawBar(SLOT_COUNT, T.good, 0);
    else if (st == TS_RUN || st == TS_SAVED || st == TS_NO_RESULT) {
        float v = st == TS_RUN ? t.speed() : t.maxKmh();
        drawBar(SLOT_COUNT * (v > 200 ? 1 : v / 200), T.accent, 0);
    } else drawBar(0, T.accent, 0);

    // split boxes: stamped when the speed is reached; READY keeps the last run on screen
    static const Seg kBoxSeg[4] = { SEG_0_100, SEG_100_120, SEG_120_160, SEG_0_200 };
    static const char *const kBoxLbl[4] = { "0-100", "100-120", "120-160", "0-200 KM/H" };
    bool showing = st == TS_RUN || t.hasResult() || st == TS_NO_RESULT;
    for (int i = 0; i < 4; i++) {
        Seg s = kBoxSeg[i];
        PanelState ps = PS_PENDING;
        float tm = -1;
        if (showing) {
            if (t.segDone(s)) {
                tm = t.segTime(s, now);
                float age = t.segAge(s, now);
                ps = age < kFlashS ? PS_FRESH : t.newBest(s) ? PS_BEST : PS_DONE;
            } else if (t.segActive(s)) ps = PS_ACTIVE;        // being chased: stamped when reached
        } else if (log.count && log.runs[0].cs[s] != RUN_NONE) {
            ps = PS_DONE;
            tm = log.runs[0].cs[s] / 100.0f;
        }
        fmtSec(num, sizeof num, tm);
        drawBox(i, kBoxLbl[i], num, ps, showing ? t.segProgress(s) : 0);
    }

    smallButton(RG_LOGBTN, R_LOGBTN, "LOG", st != TS_RUN);
    smallButton(RG_BTN, R_BTN, st == TS_RUN ? "ABORT" : t.hasResult() || st == TS_NO_RESULT ? "AGAIN" : "EXIT", true);
}

Action tapTimer(int x, int y, const DragTimer &t) {
    if (inside(R_BTN, x, y)) {
        if (t.state() == TS_RUN) return ACT_ABORT;
        return t.hasResult() || t.state() == TS_NO_RESULT ? ACT_AGAIN : ACT_EXIT;
    }
    if (inside(R_LOGBTN, x, y) && t.state() != TS_RUN) return ACT_LOG;
    return ACT_NONE;
}

// ---- log page ------------------------------------------------------------------------------
static void button(const Rect &r, const char *label, uint32_t edge, bool hot, float fillFrac = 0) {
    Canvas &c = cv();
    c.blendRect(r.x, r.y, r.w, r.h, 0x0000, 200);
    if (fillFrac > 0) c.blendRect(r.x, r.y, (int)(r.w * (fillFrac > 1 ? 1 : fillFrac)), r.h, C(edge), 110);
    uint16_t e = C(edge);
    c.fillRect(r.x, r.y, r.w, 1, e);
    c.fillRect(r.x, r.y + r.h - 1, r.w, 1, e);
    c.fillRect(r.x, r.y, 1, r.h, e);
    c.fillRect(r.x + r.w - 1, r.y, 1, r.h, e);
    c.text(font_ui, r.x + r.w / 2, r.y + (r.h + font_ui.ascent) / 2, label, C(hot ? edge : C_STATUS), ALIGN_CENTER, 1);
}

static bool s_confirm = false;             // CLEAR tapped: waiting for CONFIRM / CANCEL

static void composeLog(const RunLog &log) {
    const Theme &T = th();
    Canvas &c = cv();
    for (int i = 0; i < c.w * c.h; i++) c.px[i] = blend565(215, 0, c.px[i]);
    c.text(font_ui, 12, 20, "RUN LOG", C(T.accentBright), ALIGN_LEFT, 2);
    char buf[48];
    if (s_confirm) snprintf(buf, sizeof buf, "DELETE ALL %d RUN%s?", log.count, log.count == 1 ? "" : "S");
    else snprintf(buf, sizeof buf, "%d run%s saved", log.count, log.count == 1 ? "" : "s");
    c.text(font_small, 100, 19, buf, C(s_confirm ? 0xff2e2e : C_MUTED));
    button(L_BACK, "BACK", T.accent, true);
    for (int x = 8; x < 312; x++) c.blendPixel(x, 30, C(T.accent), (uint8_t)(255 - (x - 8) * 200 / 304));

    static const int cx[6] = { 12, 36, 82, 128, 174, 220 };
    static const char *const hdr[6] = { "#", "0-100", "100-120", "120-160", "0-200", "TOP @TIME" };
    static const Seg cols[4] = { SEG_0_100, SEG_100_120, SEG_120_160, SEG_0_200 };
    for (int k = 0; k < 6; k++) c.text(font_small, cx[k], 44, hdr[k], C(C_MUTED));
    c.fillRect(8, 48, 304, 1, C(C_EDGE));
    uint16_t best[4];
    for (int k = 0; k < 4; k++) best[k] = log.best(cols[k]);

    const int rows = log.count < 6 ? log.count : 6;     // newest 6 on screen; `timerlog` prints all
    if (!rows) c.text(font_small, 160, 110, "No runs yet - launch from READY", C(C_MUTED), ALIGN_CENTER);
    for (int r = 0; r < rows; r++) {
        const RunRecord &rec = log.runs[r];
        int y = 64 + r * 21;
        if (r == 0) c.blendRect(8, y - 12, 304, 18, C(T.accent), 40);
        snprintf(buf, sizeof buf, "%u", rec.seq);
        c.text(font_small, cx[0], y, buf, C(C_MUTED));
        for (int k = 0; k < 4; k++) {
            uint16_t v = rec.cs[cols[k]];
            fmtCs(buf, sizeof buf, v);
            uint32_t col = v == RUN_NONE ? C_DIM : v == best[k] ? T.good : 0xffffff;
            c.text(font_small, cx[k + 1], y, buf, C(col));
        }
        snprintf(buf, sizeof buf, "%u @%u.%01u", rec.maxKmh, rec.toMaxCs / 100, rec.toMaxCs % 100 / 10);
        c.text(font_small, cx[5], y, buf, C(0xffffff));
        c.fillRect(8, y + 7, 304, 1, C(0x1c252d));
    }
    if (log.count) {
        c.text(font_small, 12, 199, "BEST", C(T.good));
        for (int k = 0; k < 4; k++) {
            fmtCs(buf, sizeof buf, best[k]);
            c.text(font_small, cx[k + 1], 199, buf, C(T.good));
        }
    }
    if (s_confirm) {
        button(L_CLEAR, "CONFIRM CLEAR", 0xff2e2e, true, 1.0f);
        button(L_NEW, "CANCEL", T.accent, true);
    } else {
        button(L_CLEAR, "CLEAR LOG", 0xff2e2e, false);
        button(L_NEW, "NEW RUN", T.accent, true);
    }
}

void drawLog(const RunLog &log) {
    Canvas &c = cv();
    for (int y = 0; y < 240; y += 40) {
        c.begin(0, y, 320, 40, th().background);
        composeLog(log);
        gauge_ui::pushFn()(0, y, 320, 40, c.px);
    }
}

bool logConfirming() { return s_confirm; }

Action tapLog(int x, int y) {
    if (inside(L_BACK, x, y)) { s_confirm = false; return ACT_BACK; }
    if (s_confirm) {
        if (inside(L_CLEAR, x, y)) { s_confirm = false; return ACT_CLEAR; }
        if (inside(L_NEW, x, y))   { s_confirm = false; return ACT_CANCEL; }
        return ACT_NONE;
    }
    if (inside(L_CLEAR, x, y)) { s_confirm = true; return ACT_CLEAR_ASK; }
    if (inside(L_NEW, x, y)) return ACT_NEW_RUN;
    return ACT_NONE;
}

}  // namespace timer_ui
