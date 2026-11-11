#pragma once
// Small off-screen RGB565 canvas used to compose one screen region at a time:
// background art -> fills -> anti-aliased text, then pushed to the panel in one go.
// Pure C++ (no Arduino / TFT_eSPI) so the same drawing code runs in the host preview.
#include <stdint.h>
#include "ui/font.h"

static inline uint16_t rgb565(uint32_t rgb888) {
    return (uint16_t)(((rgb888 >> 8) & 0xF800) | ((rgb888 >> 5) & 0x07E0) | ((rgb888 >> 3) & 0x001F));
}

// a = 0 -> bg, a = 255 -> fg
uint16_t blend565(uint8_t a, uint16_t fg, uint16_t bg);
// f > 0 brightens toward white, f < 0 darkens toward black (-1..1)
uint16_t shade565(uint16_t c, float f);

enum TextAlign : uint8_t { ALIGN_LEFT, ALIGN_RIGHT, ALIGN_CENTER };

class Canvas {
public:
    // Region of the 320x240 screen this canvas currently represents.
    int x0 = 0, y0 = 0, w = 0, h = 0;
    uint16_t *px = nullptr;   // w*h pixels, plain RGB565 (not byte-swapped)

    // Attach to a caller-owned buffer of at least cap pixels.
    Canvas(uint16_t *buf, int cap) : px(buf), cap_(cap) {}

    // Start composing screen rect (x,y,w,h), pre-filled from the full-screen
    // background image. Returns false if the rect does not fit the buffer.
    bool begin(int x, int y, int rw, int rh, const uint16_t *background320x240);

    // Like begin(), but fills the region with one solid colour (no background image).
    // Used by skins that compose over a flat backdrop instead of baked art.
    bool beginSolid(int x, int y, int rw, int rh, uint16_t color);

    // All drawing below takes SCREEN coordinates and clips to the region.
    void pixel(int x, int y, uint16_t c);
    void blendPixel(int x, int y, uint16_t c, uint8_t alpha);
    void fillRect(int x, int y, int rw, int rh, uint16_t c);
    void blendRect(int x, int y, int rw, int rh, uint16_t c, uint8_t alpha);
    void hspan(float xa, float xb, int y, uint16_t c);        // AA ends, for slanted shapes
    void fillCircle(float cx, float cy, float r, uint16_t c); // AA edge

    // Text. y is the BASELINE. tabular=true gives every digit the width of the
    // widest digit so numbers don't jitter horizontally as they change.
    int  textWidth(const GaugeFont &f, const char *s, int tracking = 0, bool tabular = false) const;
    int  text(const GaugeFont &f, int x, int y, const char *s, uint16_t c,
              TextAlign align = ALIGN_LEFT, int tracking = 0, bool tabular = false);

private:
    int cap_;
};
