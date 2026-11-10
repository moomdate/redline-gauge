// Standalone Thai-rendering demo for phase 2: draws sample Thai words/marks with
// the gauge's own canvas text() path to verify combining-mark placement.
//   c++ -std=c++17 -I ../../src thai_demo.cpp ../../src/ui/canvas.cpp -o out/thai_demo
#include <stdio.h>
#include <string.h>
#include "ui/canvas.h"
#include "fonts/font_th.h"
#include "fonts/font_rpm.h"

static uint16_t fb[320 * 240];

static void save(const char *path) {
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6 320 240 255\n");
    for (int i = 0; i < 320 * 240; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3 | (c >> 13)),
                           (uint8_t)(((c >> 5) & 63) << 2 | ((c >> 9) & 3)),
                           (uint8_t)((c & 31) << 3 | ((c >> 2) & 7)) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main() {
    // dark textured-ish background
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++) {
            int v = 14 + ((x ^ y) & 3) + (y / 24);
            fb[y * 320 + x] = rgb565((v << 16) | (v << 8) | v);
        }

    static uint16_t buf[320 * 240];
    Canvas cv(buf, 320 * 240);
    cv.begin(0, 0, 320, 240, fb);

    uint16_t white = rgb565(0xFFFFFF), amber = rgb565(0xFFB020),
             cyan = rgb565(0x30D0FF), green = rgb565(0x40E060), grey = rgb565(0x9098A0);

    cv.text(font_th, 12, 26, "REDLINE \xE0\xB8\xA0\xE0\xB8\xB2\xE0\xB8\xA9\xE0\xB8\xB2\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2", amber); // ภาษาไทย
    cv.fillRect(8, 34, 304, 1, grey);

    // Gauge labels
    cv.text(font_th, 12, 62,  "\xE0\xB8\xA3\xE0\xB8\xAD\xE0\xB8\x9A\xE0\xB9\x80\xE0\xB8\x84\xE0\xB8\xA3\xE0\xB8\xB7\xE0\xB9\x88\xE0\xB8\xAD\xE0\xB8\x87", white); // รอบเครื่อง (mark-on-mark)
    cv.text(font_th, 180, 62, "7,250", white);
    cv.text(font_th, 12, 86,  "\xE0\xB8\x84\xE0\xB8\xA7\xE0\xB8\xB2\xE0\xB8\xA1\xE0\xB9\x80\xE0\xB8\xA3\xE0\xB9\x87\xE0\xB8\xA7", white); // ความเร็ว
    cv.text(font_th, 180, 86, "142 km/h", white);
    cv.text(font_th, 12, 110, "\xE0\xB8\xAD\xE0\xB8\xB8\xE0\xB8\x93\xE0\xB8\xAB\xE0\xB8\xA0\xE0\xB8\xB9\xE0\xB8\xA1\xE0\xB8\xB4", white); // อุณหภูมิ (lower+upper)
    cv.text(font_th, 180, 110, "92 \xC2\xB0""C", white);
    cv.text(font_th, 12, 134, "\xE0\xB9\x81\xE0\xB8\xA3\xE0\xB8\x87\xE0\xB8\x94\xE0\xB8\xB1\xE0\xB8\x99\xE0\xB9\x84\xE0\xB8\x9F\xE0\xB8\x9F\xE0\xB9\x89\xE0\xB8\xB2", white); // แรงดันไฟฟ้า
    cv.text(font_th, 180, 134, "13.6 V", white);
    cv.text(font_th, 12, 158, "\xE0\xB8\x95\xE0\xB8\xB1\xE0\xB9\x89\xE0\xB8\x87\xE0\xB8\x84\xE0\xB9\x88\xE0\xB8\xB2", amber); // ตั้งค่า (mark-on-mark)

    cv.fillRect(8, 170, 304, 1, grey);
    // Combining-mark torture row: tones on bare consonant, tone over upper vowel
    cv.text(font_th, 12, 196,
            "\xE0\xB8\x81\xE0\xB9\x88 \xE0\xB8\x81\xE0\xB9\x89 \xE0\xB8\x81\xE0\xB9\x8A \xE0\xB8\x81\xE0\xB9\x8B "   // ก่ ก้ ก๊ ก๋
            "\xE0\xB8\x81\xE0\xB8\xB5 \xE0\xB8\x81\xE0\xB8\xB5\xE0\xB9\x88 "                                     // กี กี่
            "\xE0\xB8\x95\xE0\xB8\xB1\xE0\xB9\x89 \xE0\xB8\x84\xE0\xB8\xA3\xE0\xB8\xB7\xE0\xB9\x88",              // ตั้ ครื่
            cyan);
    // Thai digits + colours
    cv.text(font_th, 12, 222,
            "\xE0\xB9\x90\xE0\xB9\x91\xE0\xB9\x92\xE0\xB9\x93\xE0\xB9\x94\xE0\xB9\x95\xE0\xB9\x96\xE0\xB9\x97\xE0\xB9\x98\xE0\xB9\x99  " // ๐๑๒๓๔๕๖๗๘๙
            "\xE0\xB8\x95\xE0\xB8\xB1\xE0\xB9\x89\xE0\xB8\x87\xE0\xB8\x84\xE0\xB9\x88\xE0\xB8\xB2\xE0\xB8\xAA\xE0\xB8\xB3\xE0\xB9\x80\xE0\xB8\xA3\xE0\xB9\x87\xE0\xB8\x88", green); // ตั้งค่าสำเร็จ

    cv.text(font_rpm, 250, 210, "88", amber);

    for (int r = 0; r < 240; r++) memcpy(&fb[r * 320], cv.px + r * 320, 320 * 2);
    save("out/thai_demo.ppm");
    printf("wrote out/thai_demo.ppm\n");
    return 0;
}
