#include "ui/scan_ui.h"
#include "ui/canvas.h"
#include "ui/gauge_ui.h"
#include "ui/theme.h"
#include "data/obd_source.h"
#include "fonts/font_ui.h"
#include "fonts/font_small.h"
#include <stdio.h>
#include <string.h>

namespace scan_ui {

struct Rect { int x, y, w, h; };
static const Rect M       = { 14, 12, 292, 216 };               // modal panel
static const Rect R_CLOSE = { M.x + M.w - 28, M.y + 7, 20, 18 };
static const int  LX = M.x + 12, LY = M.y + 40, ROWH = 26, ROWS = 5;
static const int  ROW_R = M.x + M.w - 34;                       // right edge of a row's content
static const Rect R_UP   = { ROW_R + 6, LY, 20, 58 };
static const Rect R_DN   = { ROW_R + 6, LY + 66, 20, 58 };
static const Rect R_RESCAN = { M.x + 12, M.y + M.h - 30, 86, 22 };
static const Rect R_PAIR   = { M.x + M.w - 98, M.y + M.h - 30, 86, 22 };

#define C_TEXT  0xd9e2e7
#define C_MUTED 0x8796a0
#define C_PANEL 0x0e141a
#define C_EDGE  0x34434e

static int s_sel = -1, s_off = 0;
static ObdSource::BtDevice s_dev[ObdSource::kMaxScan];
static int s_n = 0;
static char s_key[256];

static inline uint16_t C(uint32_t rgb) { return rgb565(rgb); }
static bool in(const Rect &r, int x, int y) { return x >= r.x - 2 && x < r.x + r.w + 2 && y >= r.y - 2 && y < r.y + r.h + 2; }

void open() { s_sel = -1; s_off = 0; s_n = 0; s_key[0] = 0; }
void invalidate() { s_key[0] = 0; }
const char *selectedMac() { return (s_sel >= 0 && s_sel < s_n) ? s_dev[s_sel].mac : ""; }

static void button(Canvas &cv, const Theme &t, const Rect &r, const char *label, bool on) {
    uint16_t fill = on ? blend565(120, C(t.accent), C(0x141b21)) : C(0x11171d);
    uint16_t edge = on ? C(t.accent) : C(C_EDGE);
    cv.fillRect(r.x, r.y, r.w, r.h, fill);
    cv.fillRect(r.x, r.y, r.w, 1, edge);
    cv.fillRect(r.x, r.y + r.h - 1, r.w, 1, edge);
    cv.fillRect(r.x, r.y, 1, r.h, edge);
    cv.fillRect(r.x + r.w - 1, r.y, 1, r.h, edge);
    cv.text(font_ui, r.x + r.w / 2, r.y + r.h / 2 + 5, label, C(on ? C_TEXT : C_MUTED), ALIGN_CENTER);
}

static void chip(Canvas &cv, int rightX, int y, const char *txt, uint16_t c) {
    int w = cv.textWidth(font_small, txt) + 8;
    cv.blendRect(rightX - w, y, w, 12, c, 55);
    cv.text(font_small, rightX - w + 4, y + 9, txt, c);
}

static void compose(Canvas &cv, const Theme &t, int state) {
    for (int i = 0; i < cv.w * cv.h; i++) cv.px[i] = blend565(150, 0, cv.px[i]);   // dim the gauge behind
    cv.blendRect(M.x, M.y, M.w, M.h, C(C_PANEL), 242);
    cv.fillRect(M.x, M.y, M.w, 1, C(t.accent));
    cv.fillRect(M.x, M.y + M.h - 1, M.w, 1, C(C_EDGE));
    cv.fillRect(M.x, M.y, 1, M.h, C(C_EDGE));
    cv.fillRect(M.x + M.w - 1, M.y, 1, M.h, C(C_EDGE));
    cv.text(font_ui, M.x + 12, M.y + 21, "BLUETOOTH", C(t.accentBright), ALIGN_LEFT, 1);
    button(cv, t, R_CLOSE, "X", true);

    if (state != 2) {
        cv.text(font_small, M.x + 16, LY + 34, "Scanning for adapters (10 s)...", C(C_TEXT));
        cv.text(font_small, M.x + 16, LY + 52, "Turn the adapter on; close any phone OBD app.", C(C_MUTED));
        button(cv, t, R_RESCAN, "RESCAN", false);
        button(cv, t, R_PAIR, "PAIR", false);
        return;
    }
    if (s_n == 0) {
        cv.text(font_small, M.x + 16, LY + 34, "No devices found.", C(C_TEXT));
        cv.text(font_small, M.x + 16, LY + 52, "Powered? Not already paired to a phone?", C(C_MUTED));
    }
    for (int r = 0; r < ROWS; r++) {
        int idx = s_off + r;
        if (idx >= s_n) break;
        int ry = LY + r * ROWH;
        if (idx == s_sel) cv.blendRect(LX - 4, ry - 1, ROW_R - LX + 8, ROWH - 3, C(t.accent), 60);
        const ObdSource::BtDevice &d = s_dev[idx];
        cv.text(font_small, LX, ry + 13, d.name, C(d.name[0] == '(' ? C_MUTED : C_TEXT));
        int rightX = ROW_R;
        if (d.isObd) { chip(cv, ROW_R, ry + 2, "OBD", C(t.good)); rightX = ROW_R - 34; }
        char rs[8];
        snprintf(rs, sizeof rs, "%d", d.rssi);
        cv.text(font_small, rightX, ry + 13, rs, C(C_MUTED), ALIGN_RIGHT);
    }
    if (s_n > ROWS) {
        button(cv, t, R_UP, "-", s_off > 0);
        button(cv, t, R_DN, "+", s_off + ROWS < s_n);
    }
    button(cv, t, R_RESCAN, "RESCAN", true);
    button(cv, t, R_PAIR, "PAIR", s_sel >= 0);
    cv.text(font_small, M.x + M.w / 2, M.y + M.h - 16, s_sel >= 0 ? "PIN auto" : "tap a device",
            C(C_MUTED), ALIGN_CENTER);
}

void render(uint32_t now, const Settings &s) {
    (void)now;
    int state = ObdSource::scanState();
    if (state == 2) s_n = ObdSource::scanResults(s_dev, ObdSource::kMaxScan);
    char key[sizeof s_key];
    int k = snprintf(key, sizeof key, "%d|%d|%d|%d|%d", state, s_n, s_sel, s_off, s.theme);
    for (int i = 0; i < s_n && k < (int)sizeof key - 20; i++)
        k += snprintf(key + k, sizeof key - k, "|%.5s%d", s_dev[i].mac + 12, s_dev[i].rssi);
    if (!strcmp(key, s_key)) return;
    strcpy(s_key, key);

    const Theme &t = kThemes[s.theme];
    Canvas &cv = gauge_ui::canvas();
    PushFn push = gauge_ui::pushFn();
    for (int y = 0; y < 240; y += 40) {
        cv.begin(0, y, 320, 40, t.background);
        compose(cv, t, state);
        push(0, y, 320, 40, cv.px);
    }
}

Action tap(int x, int y) {
    if (in(R_CLOSE, x, y))  return ACT_CLOSE;
    if (in(R_RESCAN, x, y)) { s_sel = -1; s_off = 0; s_key[0] = 0; return ACT_RESCAN; }
    if (ObdSource::scanState() == 2) {
        if (s_n > ROWS && in(R_UP, x, y)) { if (s_off > 0) { s_off--; s_key[0] = 0; } return ACT_NONE; }
        if (s_n > ROWS && in(R_DN, x, y)) { if (s_off + ROWS < s_n) { s_off++; s_key[0] = 0; } return ACT_NONE; }
        for (int r = 0; r < ROWS; r++) {
            int idx = s_off + r, ry = LY + r * ROWH;
            if (idx < s_n && x >= LX - 4 && x < ROW_R + 4 && y >= ry - 1 && y < ry + ROWH - 3) {
                s_sel = idx; s_key[0] = 0; return ACT_NONE;
            }
        }
        if (s_sel >= 0 && in(R_PAIR, x, y)) return ACT_PAIR;
    }
    return ACT_NONE;
}

}  // namespace scan_ui
