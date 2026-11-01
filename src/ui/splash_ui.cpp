#include "ui/splash_ui.h"
#include "ui/canvas.h"
#include "config.h"
#include "assets/logo_birdlab.h"
#include "fonts/font_title.h"
#include "core/provenance.h"
#include "fonts/font_small.h"
#include <string.h>

namespace splash_ui {

extern const uint32_t kCreditCrc;   // expected value, kept in theme.cpp
static bool s_completed = false;
static const char *const kCredit = "crafted by";
static const char *const kWordmark = "REDLINE";

static uint32_t crc32(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

uint32_t creditCrc() {
    uint32_t c = crc32(0, logo_birdlab, sizeof logo_birdlab);
    c = crc32(c, (const uint8_t *)kCredit, strlen(kCredit));
    return crc32(c, (const uint8_t *)kWordmark, strlen(kWordmark));
}

bool authentic() { return s_completed && creditCrc() == kCreditCrc && provenance::intact(); }

#define C_RED    0xff2424
#define C_WHITE  0xffffff
#define C_MUTED  0x8796a0
#define C_SLOT   0x1c252d
#define C_LOGO   0xedf2f5

// mini shift bar: 13 slanted slots, same colour zones as the gauge's bar
static const int kSlots = 13, kSlotW = 12, kGap = 3, kSkew = 4;
static const int kBarY = 124, kBarH = 9;
static const int kBarW = kSlots * (kSlotW + kGap) - kGap + kSkew;
static const int kBarX = (320 - kBarW) / 2;

static inline uint16_t C(uint32_t rgb) { return rgb565(rgb); }

static uint32_t slotColor(const Theme &t, int s) {
    if (s >= kSlots - 2) return C_RED;
    if (s >= kSlots - 4) return t.warning;
    return t.accent;
}

// Everything is drawn in screen coordinates and clips itself to the canvas,
// so the same code paints full-screen strips and the small progress region.
static void backdrop(Canvas &cv) {
    for (int i = 0; i < cv.w * cv.h; i++) cv.px[i] = blend565(222, 0, cv.px[i]);
}

static void bar(Canvas &cv, const Theme &t, float p) {
    float lit = p * kSlots;
    for (int r = 0; r < kBarH; r++) {
        int y = kBarY + r;
        float shift = kSkew * (1.0f - (float)r / (kBarH - 1));   // top leans right
        float sh = r == 0 ? 0.35f : 0.05f - 0.25f * r / kBarH;
        for (int s = 0; s < kSlots; s++) {
            float a = kBarX + s * (kSlotW + kGap) + shift, b = a + kSlotW;
            cv.hspan(a, b, y, C(C_SLOT));
            float f = lit - s;
            if (f > 0) cv.hspan(a, a + (f > 1 ? 1 : f) * kSlotW, y, shade565(C(slotColor(t, s)), sh));
        }
    }
}

static void compose(Canvas &cv, const Theme &t, float p) {
    backdrop(cv);

    // wordmark: RED in red, LINE in white, soft drop shadow
    int wRed = cv.textWidth(font_title, "RED", 1), wAll = cv.textWidth(font_title, kWordmark, 1);
    int x = (320 - wAll) / 2, base = 104;
    cv.text(font_title, x + 2, base + 3, kWordmark, 0x0000, ALIGN_LEFT, 1);
    cv.text(font_title, x, base, "RED", C(C_RED), ALIGN_LEFT, 1);
    cv.text(font_title, x + wRed + 1, base, "LINE", C(C_WHITE), ALIGN_LEFT, 1);

    bar(cv, t, p);

    cv.text(font_small, 160, 154, "RACING DASH FOR THE CYD", C(C_MUTED), ALIGN_CENTER, 2);
    cv.text(font_small, 160, 168, "v" REDLINE_VERSION, C(t.accentBright), ALIGN_CENTER, 1);

    // credit: "crafted by" + birdlab.th logo (alpha mask tinted light grey)
    const char *label = kCredit;
    int tw = cv.textWidth(font_small, label, 1);
    int gx = (320 - (tw + 8 + LOGO_BIRDLAB_W)) / 2, ly = 200;
    cv.text(font_small, gx, ly + LOGO_BIRDLAB_H / 2 + 3, label, C(C_MUTED), ALIGN_LEFT, 1);
    uint16_t lc = C(C_LOGO);
    for (int y = 0; y < LOGO_BIRDLAB_H; y++)
        for (int xx = 0; xx < LOGO_BIRDLAB_W; xx++) {
            uint8_t a = logo_birdlab[y * LOGO_BIRDLAB_W + xx];
            if (a) cv.blendPixel(gx + tw + 8 + xx, ly + y, lc, a);
        }
}

void draw(PushFn push, const Theme &t) {
    Canvas &cv = gauge_ui::canvas();
    for (int y = 0; y < 240; y += 40) {
        cv.begin(0, y, 320, 40, t.background);
        compose(cv, t, 0);
        push(0, y, 320, 40, cv.px);
    }
}

void progress(PushFn push, const Theme &t, float p) {
    Canvas &cv = gauge_ui::canvas();
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    if (p >= 1) s_completed = true;
    if (!cv.begin(kBarX - 2, kBarY - 1, kBarW + 4, kBarH + 2, t.background)) return;
    backdrop(cv);
    bar(cv, t, p);
    push(cv.x0, cv.y0, cv.w, cv.h, cv.px);
}

}  // namespace splash_ui
