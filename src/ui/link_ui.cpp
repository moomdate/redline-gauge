#include "ui/link_ui.h"
#include "ui/canvas.h"
#include "ui/gauge_ui.h"
#include "ui/theme.h"
#include "fonts/font_ui.h"
#include "fonts/font_small.h"
#include "fonts/font_value.h"
#include <stdio.h>
#include <string.h>

namespace link_ui {

struct Rect { int x, y, w, h; };
static const Rect R_DONE = { 250, 4, 64, 22 };
static const Rect R_SCAN = { 170, 4, 74, 22 };   // OBD BT only: open the pair modal
static bool s_isObd = false;
static const Rect R_CARD = { 8, 38, 304, 62 };

#define C_TEXT   0xd9e2e7
#define C_MUTED  0x8796a0
#define C_PANEL  0x10161b
#define C_EDGE   0x34434e
#define C_CRIT   0xff2e2e

static char s_key[400];

static inline uint16_t C(uint32_t rgb) { return rgb565(rgb); }

static bool inside(const Rect &r, int x, int y) {
    return x >= r.x - 3 && x < r.x + r.w + 3 && y >= r.y - 3 && y < r.y + r.h + 3;
}

// Everything the page shows, as text, so "did anything change" is a string compare.
struct Page {
    uint32_t color;
    int      icon;                 // 0 live, 1 connecting, 2 error
    char     title[16], line1[56], line2[56];
    char     uptime[12], rate[12], last[12], links[16];
};

static void fmtDuration(char *out, size_t n, uint32_t ms) {
    uint32_t s = ms / 1000;
    if (s >= 3600) snprintf(out, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    else snprintf(out, n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

static Page build(const LinkStatus &k, uint32_t now, const Theme &t) {
    Page p = {};
    switch (k.state) {
    case LS_LIVE:
        p.color = t.good; p.icon = 0;
        snprintf(p.title, sizeof p.title, "CONNECTED");
        snprintf(p.line1, sizeof p.line1, "The ECU answers: live data on the gauge");
        snprintf(p.line2, sizeof p.line2, "Checked on every reading");
        break;
    case LS_RETRY: {
        p.color = C_CRIT; p.icon = 2;
        int32_t left = (int32_t)(k.retryAt - now);
        int sec = left > 0 ? (left + 999) / 1000 : 0;
        snprintf(p.title, sizeof p.title, "%s", k.reason[0] ? k.reason : "NO ECU");
        snprintf(p.line1, sizeof p.line1, "%s", k.hint);
        if (sec > 0) snprintf(p.line2, sizeof p.line2, "Trying again in %d s", sec);
        else snprintf(p.line2, sizeof p.line2, "Trying again now");
        break;
    }
    default:
        p.color = t.warning; p.icon = 1;
        snprintf(p.title, sizeof p.title, "CONNECTING");
        snprintf(p.line1, sizeof p.line1, "Step: %s", k.step[0] ? k.step : "starting");
        snprintf(p.line2, sizeof p.line2, "Retries on its own, no restart needed");
        break;
    }
    if (k.state == LS_LIVE) fmtDuration(p.uptime, sizeof p.uptime, now - k.liveSince);
    else snprintf(p.uptime, sizeof p.uptime, "--");
    if (k.state == LS_LIVE) snprintf(p.rate, sizeof p.rate, "%u /s", k.rate);
    else snprintf(p.rate, sizeof p.rate, "--");
    if (k.lastReply) {
        uint32_t age = (now - k.lastReply) / 1000;
        if (age == 0) snprintf(p.last, sizeof p.last, "now");
        else snprintf(p.last, sizeof p.last, "%u s ago", (unsigned)(age > 999 ? 999 : age));
    } else snprintf(p.last, sizeof p.last, "never");
    snprintf(p.links, sizeof p.links, "%u / %u", k.connects, k.drops);
    return p;
}

static void button(Canvas &cv, const Theme &t, const Rect &r, const char *label) {
    const int cut = 5;
    uint16_t fill = blend565(90, C(t.accent), C(0x141b21)), edge = C(t.accent);
    for (int y = 0; y < r.h; y++) {
        int inset = y < cut ? cut - y : 0;
        cv.fillRect(r.x + inset, r.y + y, r.w - inset, 1, fill);
        cv.pixel(r.x + inset, r.y + y, edge);
        cv.pixel(r.x + r.w - 1, r.y + y, edge);
    }
    cv.fillRect(r.x + cut, r.y, r.w - cut, 1, edge);
    cv.fillRect(r.x, r.y + r.h - 1, r.w, 1, edge);
    cv.fillRect(r.x + 1, r.y + r.h - 3, r.w - 2, 2, C(t.accent));
    cv.text(font_ui, r.x + r.w / 2, r.y + 16, label, C(C_TEXT), ALIGN_CENTER);
}

static void bigIcon(Canvas &cv, int icon, float cx, float cy, uint16_t c) {
    cv.fillCircle(cx, cy, 13, blend565(70, c, C(C_PANEL)));
    if (icon == 0) {                                     // tick
        for (int i = 0; i <= 4; i++) cv.fillCircle(cx - 6 + i, cy + i - 1, 1.4f, c);
        for (int i = 0; i <= 8; i++) cv.fillCircle(cx - 2 + i, cy + 3 - i, 1.4f, c);
    } else if (icon == 1) {                              // ring
        cv.fillCircle(cx, cy, 7, c);
        cv.fillCircle(cx, cy, 4.5f, blend565(70, c, C(C_PANEL)));
    } else {                                             // cross
        for (int i = -6; i <= 6; i++) {
            cv.fillCircle(cx + i, cy + i, 1.4f, c);
            cv.fillCircle(cx + i, cy - i, 1.4f, c);
        }
    }
}

// one key / value row: key muted on the left, value right-aligned in the column
static void row(Canvas &cv, int x, int y, int w, const char *k, const char *v, uint32_t vc = C_TEXT) {
    cv.text(font_small, x, y, k, C(C_MUTED), ALIGN_LEFT, 1);
    cv.text(font_small, x + w, y, v, C(vc), ALIGN_RIGHT);
    for (int i = x; i < x + w; i += 2) cv.pixel(i, y + 5, C(0x222c33));     // dotted rule
}

static void compose(Canvas &cv, const Page &p, const LinkStatus &k, const Theme &t) {
    for (int i = 0; i < cv.w * cv.h; i++) cv.px[i] = blend565(215, 0, cv.px[i]);   // darkened art

    int tw = cv.text(font_ui, 12, 20, k.title, C(t.accentBright), ALIGN_LEFT, 2);
    cv.text(font_small, 12 + tw + 10, 19, k.subtitle, C(C_MUTED), ALIGN_LEFT, 1);
    button(cv, t, R_DONE, "DONE");
    if (!strcmp(k.title, "OBD BT")) button(cv, t, R_SCAN, "BT SCAN");
    for (int x = 8; x < 312; x++) cv.blendPixel(x, 30, C(t.accent), (uint8_t)(255 - (x - 8) * 200 / 304));

    // status card: tinted panel with a coloured left rail
    const Rect &r = R_CARD;
    uint16_t c = C(p.color);
    cv.blendRect(r.x, r.y, r.w, r.h, C(C_PANEL), 220);
    cv.blendRect(r.x, r.y, r.w, r.h, c, 22);
    cv.fillRect(r.x, r.y, 3, r.h, c);
    cv.fillRect(r.x, r.y, r.w, 1, C(C_EDGE));
    cv.fillRect(r.x, r.y + r.h - 1, r.w, 1, C(C_EDGE));
    bigIcon(cv, p.icon, r.x + 26, r.y + 22, c);
    cv.text(font_value, r.x + 48, r.y + 31, p.title, c);
    cv.text(font_small, r.x + 14, r.y + 48, p.line1, C(C_TEXT));
    cv.text(font_small, r.x + 14, r.y + 58, p.line2, C(C_MUTED));

    // details, two columns: the link (same for every source), then the source's own
    const int y0 = 130, dy = 22, lx = 12, rx = 168, cw = 140;
    cv.text(font_small, lx, 112, "LINK", C(t.accentBright), ALIGN_LEFT, 1);
    cv.text(font_small, rx, 112, "DETAILS", C(t.accentBright), ALIGN_LEFT, 1);
    row(cv, lx, y0,          cw, "UPTIME",        p.uptime);
    row(cv, lx, y0 + dy,     cw, "REPLIES",       p.rate);
    row(cv, lx, y0 + dy * 2, cw, "LAST REPLY",    p.last);
    row(cv, lx, y0 + dy * 3, cw, "LINKS / DROPS", p.links);
    for (int i = 0; i < k.rowCount && i < 4; i++)
        row(cv, rx, y0 + dy * i, cw, k.rows[i].key, k.rows[i].value, k.rows[i].bad ? C_CRIT : C_TEXT);
}

void invalidate() { s_key[0] = 0; }

void render(const LinkStatus &st, uint32_t now, const Settings &s) {
    s_isObd = !strcmp(st.title, "OBD BT");
    const Theme &t = kThemes[s.theme];
    Page p = build(st, now, t);
    char key[sizeof s_key];
    int n = snprintf(key, sizeof key, "%u|%s|%s|%s|%s|%s|%s|%s|%d|%s", (unsigned)p.color, p.title, p.line1,
                     p.line2, p.uptime, p.rate, p.last, p.links, s.theme, st.title);
    for (int i = 0; i < st.rowCount && n > 0 && n < (int)sizeof key; i++)
        n += snprintf(key + n, sizeof key - n, "|%s%d", st.rows[i].value, st.rows[i].bad);
    if (!strcmp(key, s_key)) return;
    strcpy(s_key, key);
    Canvas &cv = gauge_ui::canvas();
    PushFn push = gauge_ui::pushFn();
    for (int y = 0; y < 240; y += 40) {                  // 40-row strips, like the settings page
        cv.begin(0, y, 320, 40, t.background);
        compose(cv, p, st, t);
        push(0, y, 320, 40, cv.px);
    }
}

Action tap(int x, int y) {
    if (inside(R_DONE, x, y)) return ACT_BACK;
    if (s_isObd && inside(R_SCAN, x, y)) return ACT_SCAN;
    return ACT_NONE;
}

}  // namespace link_ui
