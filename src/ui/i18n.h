#pragma once
// Tiny UI language switch (EN / TH). Thai strings render with font_th (Kanit SemiBold, which also
// carries Latin + digits). Pick the string with T(en, th) and the font with txtFont(enFont).
#include "fonts/font_th.h"
#include "fonts/font_th_small.h"
#include "ui/font.h"
#include <stdint.h>

namespace i18n {
inline uint8_t lang = 0;                       // 0 = EN, 1 = TH
inline void set(uint8_t l) { lang = l ? 1 : 0; }
inline const char *T(const char *en, const char *th) { return lang ? th : en; }
// Use a Thai font when TH is on (so mixed Thai/Latin renders), else the given font.
// Thai glyphs run taller than the Latin caption fonts, so compact labels (small Latin
// fonts) map to the 12px Thai cut and larger headings to the 18px one.
inline const GaugeFont &font(const GaugeFont &en) {
    if (!lang) return en;
    return en.ascent <= 10 ? font_th_small : font_th;
}
}
