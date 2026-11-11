#include "ui/canvas.h"
#include <math.h>
#include <string.h>

uint16_t blend565(uint8_t a, uint16_t fg, uint16_t bg) {
    if (a == 255) return fg;
    if (a == 0) return bg;
    // Same trick as TFT_eSPI::alphaBlend: spread to 0x07E0F81F so R, G, B mix in one multiply.
    uint32_t a5 = (a + 4) >> 3;
    uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81F;
    uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81F;
    uint32_t r = ((((f - b) * a5) >> 5) + b) & 0x07E0F81F;
    return (uint16_t)(r | (r >> 16));
}

uint16_t shade565(uint16_t c, float f) {
    if (f > 0) return blend565((uint8_t)(f * 255), 0xFFFF, c);
    if (f < 0) return blend565((uint8_t)(-f * 255), 0x0000, c);
    return c;
}

bool Canvas::begin(int x, int y, int rw, int rh, const uint16_t *bg) {
    if (rw <= 0 || rh <= 0 || rw * rh > cap_) return false;
    x0 = x; y0 = y; w = rw; h = rh;
    for (int r = 0; r < rh; r++)
        memcpy(px + r * rw, bg + (y + r) * 320 + x, rw * sizeof(uint16_t));
    return true;
}

bool Canvas::beginSolid(int x, int y, int rw, int rh, uint16_t color) {
    if (rw <= 0 || rh <= 0 || rw * rh > cap_) return false;
    x0 = x; y0 = y; w = rw; h = rh;
    for (int i = 0; i < rw * rh; i++) px[i] = color;
    return true;
}

void Canvas::pixel(int x, int y, uint16_t c) {
    x -= x0; y -= y0;
    if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) px[y * w + x] = c;
}

void Canvas::blendPixel(int x, int y, uint16_t c, uint8_t a) {
    x -= x0; y -= y0;
    if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) {
        uint16_t &d = px[y * w + x];
        d = blend565(a, c, d);
    }
}

void Canvas::fillRect(int x, int y, int rw, int rh, uint16_t c) {
    int xa = x - x0, ya = y - y0, xb = xa + rw, yb = ya + rh;
    if (xa < 0) xa = 0;
    if (ya < 0) ya = 0;
    if (xb > w) xb = w;
    if (yb > h) yb = h;
    for (int r = ya; r < yb; r++)
        for (int q = xa; q < xb; q++) px[r * w + q] = c;
}

void Canvas::blendRect(int x, int y, int rw, int rh, uint16_t c, uint8_t a) {
    for (int r = y; r < y + rh; r++)
        for (int q = x; q < x + rw; q++) blendPixel(q, r, c, a);
}

void Canvas::hspan(float xa, float xb, int y, uint16_t c) {
    if (xb <= xa) return;
    // cheap reject: off this canvas's rows or columns (common when composing a narrow strip)
    if ((unsigned)(y - y0) >= (unsigned)h || xb < x0 || xa >= x0 + w) return;
    int ia = (int)floorf(xa), ib = (int)floorf(xb);
    if (ia == ib) { blendPixel(ia, y, c, (uint8_t)((xb - xa) * 255)); return; }
    blendPixel(ia, y, c, (uint8_t)((1.0f - (xa - ia)) * 255));
    int lo = ia + 1 > x0 ? ia + 1 : x0, hi = ib < x0 + w ? ib : x0 + w;
    uint16_t *row = px + (y - y0) * w - x0;
    for (int x = lo; x < hi; x++) row[x] = c;            // solid middle: direct row writes
    float tail = xb - ib;
    if (tail > 0.004f) blendPixel(ib, y, c, (uint8_t)(tail * 255));
}

void Canvas::fillCircle(float cx, float cy, float r, uint16_t c) {
    for (int y = (int)floorf(cy - r - 1); y <= (int)ceilf(cy + r + 1); y++)
        for (int x = (int)floorf(cx - r - 1); x <= (int)ceilf(cx + r + 1); x++) {
            float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
            float cov = r + 0.5f - d;
            if (cov >= 1) pixel(x, y, c);
            else if (cov > 0) blendPixel(x, y, c, (uint8_t)(cov * 255));
        }
}

// ---- text ----------------------------------------------------------------

static const GaugeGlyph *findGlyph(const GaugeFont &f, uint16_t code) {
    int lo = 0, hi = f.count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint16_t c = f.glyphs[mid].code;
        if (c == code) return &f.glyphs[mid];
        if (c < code) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

// Minimal UTF-8 decoder (1-3 byte sequences are all this UI ever uses).
static uint16_t nextCode(const char *&s) {
    uint8_t c = (uint8_t)*s++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && *s) return (uint16_t)(((c & 0x1F) << 6) | ((uint8_t)*s++ & 0x3F));
    if ((c & 0xF0) == 0xE0 && s[0] && s[1]) {
        uint16_t u = (uint16_t)(((c & 0x0F) << 12) | (((uint8_t)s[0] & 0x3F) << 6) | ((uint8_t)s[1] & 0x3F));
        s += 2;
        return u;
    }
    return '?';
}

static int digitCell(const GaugeFont &f) {
    int m = 0;
    for (char d = '0'; d <= '9'; d++) {
        const GaugeGlyph *g = findGlyph(f, d);
        if (g && g->adv > m) m = g->adv;
    }
    return m;
}

static inline bool isDigit(uint16_t c) { return c >= '0' && c <= '9'; }

// A zero-advance glyph is a combining mark (Thai vowels/tones) that stacks on
// the preceding base glyph without moving the pen.
static inline bool isCombining(const GaugeGlyph *g) { return g && g->adv == 0; }

// Alpha-blend one glyph bitmap onto the canvas at pen-relative (gx, gy).
static void blitGlyph(uint16_t *px, int w, int h, int x0, int y0,
                      const GaugeFont &f, const GaugeGlyph *g, int gx, int gy, uint16_t c) {
    if (gx + g->w <= x0 || gx >= x0 + w || gy + g->h <= y0 || gy >= y0 + h) return;
    const uint8_t *bm = f.bitmap + g->offset;
    for (int r = 0; r < g->h; r++) {
        int sy = gy + r - y0;
        if ((unsigned)sy >= (unsigned)h) continue;
        for (int q = 0; q < g->w; q++) {
            uint8_t a = bm[r * g->w + q];
            if (!a) continue;
            int sx = gx + q - x0;
            if ((unsigned)sx >= (unsigned)w) continue;
            uint16_t &d = px[sy * w + sx];
            d = blend565(a, c, d);
        }
    }
}

int Canvas::textWidth(const GaugeFont &f, const char *s, int tracking, bool tabular) const {
    int cell = tabular ? digitCell(f) : 0, wsum = 0, n = 0;
    while (*s) {
        uint16_t code = nextCode(s);
        const GaugeGlyph *g = findGlyph(f, code);
        if (isCombining(g)) continue;                     // overlays base, no width/tracking
        int adv = g ? g->adv : f.ascent / 3;
        if (tabular && isDigit(code)) adv = cell;
        wsum += adv + tracking;
        n++;
    }
    return n ? wsum - tracking : 0;
}

int Canvas::text(const GaugeFont &f, int x, int y, const char *s, uint16_t c,
                 TextAlign align, int tracking, bool tabular) {
    int tw = textWidth(f, s, tracking, tabular);
    if (align == ALIGN_RIGHT) x -= tw;
    else if (align == ALIGN_CENTER) x -= tw / 2;
    int cell = tabular ? digitCell(f) : 0;
    int stackedUppers = 0;                                // upper marks already placed on current base
    int liftStep = f.ascent / 3;                          // vertical gap when a tone stacks over a vowel
    if (liftStep < 3) liftStep = 3;

    while (*s) {
        uint16_t code = nextCode(s);
        const GaugeGlyph *g = findGlyph(f, code);
        if (!g) { x += f.ascent / 3 + tracking; stackedUppers = 0; continue; }

        if (isCombining(g)) {
            // Combining mark: Pillow places it with a negative left bearing so it
            // reaches back over the base already drawn at the current pen. Don't advance.
            bool upper = g->dy > 2;                        // above baseline -> upper vowel/tone
            int lift = (upper && stackedUppers > 0) ? stackedUppers * liftStep : 0;
            int gx = x + g->dx, gy = y - g->dy - lift;
            blitGlyph(px, w, h, x0, y0, f, g, gx, gy, c);
            if (upper) stackedUppers++;
            continue;                                      // no advance, no tracking
        }
        stackedUppers = 0;                                 // new base resets the stack

        int adv = g->adv, ox = 0;
        if (tabular && isDigit(code)) { ox = (cell - g->adv) / 2; adv = cell; }

        int gx = x + ox + g->dx, gy = y - g->dy;
        blitGlyph(px, w, h, x0, y0, f, g, gx, gy, c);
        x += adv + tracking;
    }
    return tw;
}
