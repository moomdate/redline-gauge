#include "ui/touch_cal_ui.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>
#include "hw/xpt2046_touch.h"

namespace touch_cal_ui {

static const int W = 320, H = 240, M = TOUCH_CAL_MARGIN;
static const int PX[4] = { M, W - M, M, W - M };
static const int PY[4] = { M, M, H - M, H - M };
static const int BTN_Y = 196, BTN_H = 36;
static const int SAVE_X = 70, AGAIN_X = 170, BTN_W = 80;
static const uint32_t IDLE_CANCEL_MS = 60000;
static const float    TOUCH_CAL_MAX_OFF_PX = 12;   // centre check tolerance

enum Outcome { GOT, BOOT, IDLE };

static void apply(Xpt2046Touch &t, const TouchCal &c) {
    t.setCal(c.xmin, c.xmax, c.ymin, c.ymax, W, H, c.axis);
}

static bool bootDown(int pin) {
    if (pin < 0 || digitalRead(pin) != LOW) return false;
    delay(30);
    if (digitalRead(pin) != LOW) return false;
    while (digitalRead(pin) == LOW) delay(10);          // act on release
    return true;
}

static void cross(TFT_eSPI &tft, int x, int y, uint16_t c) {
    tft.drawFastHLine(x - 14, y, 29, c);
    tft.drawFastVLine(x, y - 14, 29, c);
    tft.drawCircle(x, y, 7, c);
}

static void centred(TFT_eSPI &tft, const char *s, int y, uint16_t c, int size) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(size);
    tft.setTextColor(c, TFT_BLACK);
    tft.drawString(s, W / 2, y, 1);
}

static void button(TFT_eSPI &tft, int x, const char *label, uint16_t fill) {
    tft.fillRoundRect(x, BTN_Y, BTN_W, BTN_H, 6, fill);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, fill);
    tft.drawString(label, x + BTN_W / 2, BTN_Y + BTN_H / 2, 1);
}

static long median(long *v, int n) {
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { long t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    return v[n / 2];
}

// One press: the median of both raw channels, skipping the first quarter of the samples (the
// finger is still settling). Gaps up to 80 ms are part of the same press, and a press counts once
// it has 8 good samples (it stops collecting at 25). BOOT or a long idle aborts.
static Outcome samplePress(Xpt2046Touch &touch, int bootPin, long &a, long &b, const char *&why) {
    uint32_t since = millis();
    long sa[25], sb[25];
    int n = 0;
    uint32_t lastGood = 0;
    for (;;) {
        if (bootDown(bootPin)) { why = "BOOT pressed"; return BOOT; }
        if (n == 0 && millis() - since > IDLE_CANCEL_MS) { why = "no tap for 60 s"; return IDLE; }
        if (touch.Pressed()) {
            if (n == 0) Serial.printf("[touch]   press seen at raw %u, %u\n", touch.RawX(), touch.RawY());
            sa[n] = touch.RawX(); sb[n] = touch.RawY();
            lastGood = millis();
            if (++n >= 25) break;
        } else if (n > 0 && millis() - lastGood > 80) {
            if (n >= 8) break;                         // short but usable tap
            Serial.printf("[touch]   press too short (%d samples), tap and hold a moment\n", n);
            n = 0;
        }
        delay(5);
    }
    int skip = n / 4;
    a = median(sa + skip, n - skip); b = median(sb + skip, n - skip);
    while (touch.Pressed()) delay(10);
    delay(200);
    return GOT;
}

bool run(TFT_eSPI &tft, Xpt2046Touch &touch, TouchCal &cal, int bootPin) {
    const TouchCal old = cal;
    if (bootPin >= 0) pinMode(bootPin, INPUT_PULLUP);
    Serial.println("[touch] calibration: tap the 4 crosses (BOOT = cancel)");

    for (;;) {
        // ---- 4 crosses. axis 1 so RawX / RawY are always channels 0x90 / 0xD0 ----
        apply(touch, TouchCal{ 0, 4095, 0, 4095, 1 });
        long a[4], b[4];
        tft.fillScreen(TFT_BLACK);
        centred(tft, "TOUCH CALIBRATION", 84, TFT_WHITE, 2);
        centred(tft, "BOOT button = cancel", 160, TFT_DARKGREY, 1);
        bool aborted = false;
        for (int i = 0; i < 4 && !aborted; i++) {
            char msg[40];
            snprintf(msg, sizeof msg, "Tap the red cross  %d / 4", i + 1);
            centred(tft, msg, 120, TFT_YELLOW, 2);
            cross(tft, PX[i], PY[i], TFT_RED);
            const char *why = "";
            Outcome o = samplePress(touch, bootPin, a[i], b[i], why);
            if (o != GOT) { aborted = true; Serial.printf("[touch] cancel: %s\n", why); break; }
            cross(tft, PX[i], PY[i], TFT_DARKGREY);
            Serial.printf("[touch] point %d: raw %ld, %ld\n", i + 1, a[i], b[i]);
        }
        if (aborted) { cal = old; apply(touch, cal); Serial.println("[touch] calibration cancelled"); return false; }

        TouchCal next;
        bool ok = touchCalSolve(a, b, next);
        Serial.printf("[touch] result setCal(%d, %d, %d, %d, %d)%s\n", next.xmin, next.xmax, next.ymin,
                      next.ymax, next.axis, ok ? "" : "  -- range too small, try again");
        if (!ok) {
            tft.fillScreen(TFT_BLACK);
            centred(tft, "Taps missed the crosses", 100, TFT_RED, 2);
            centred(tft, "Starting again...", 130, TFT_WHITE, 2);
            delay(1800);
            continue;
        }

        // ---- 5th cross in the centre checks the result before it can be saved ----
        tft.fillScreen(TFT_BLACK);
        centred(tft, "Last one: tap the centre", 84, TFT_YELLOW, 2);
        cross(tft, W / 2, H / 2, TFT_RED);
        long ca, cb;
        const char *why = "";
        if (samplePress(touch, bootPin, ca, cb, why) != GOT) {
            Serial.printf("[touch] cancel: %s\n", why);
            cal = old; apply(touch, cal); Serial.println("[touch] calibration cancelled");
            return false;
        }
        long cxr = next.axis ? ca : cb, cyr = next.axis ? cb : ca;
        int px = touchCalMap(cxr, next.xmin, next.xmax, W), py = touchCalMap(cyr, next.ymin, next.ymax, H);
        float off = sqrtf((float)(px - W / 2) * (px - W / 2) + (float)(py - H / 2) * (py - H / 2));
        Serial.printf("[touch] centre check: raw %ld, %ld -> %d, %d (%.0f px off)\n", ca, cb, px, py, off);
        if (off > TOUCH_CAL_MAX_OFF_PX) {
            char msg[40];
            snprintf(msg, sizeof msg, "Centre is %.0f px off", off);
            tft.fillScreen(TFT_BLACK);
            centred(tft, msg, 100, TFT_RED, 2);
            centred(tft, "Press firmly. Again...", 130, TFT_WHITE, 2);
            delay(1800);
            continue;
        }

        // ---- test with the new values: draw, then SAVE / AGAIN ----
        apply(touch, next);
        tft.fillScreen(TFT_BLACK);
        centred(tft, "Draw to test. Dots under your finger?", 14, TFT_WHITE, 1);
        char line[48];
        snprintf(line, sizeof line, "%d %d %d %d  axis %d", next.xmin, next.xmax, next.ymin, next.ymax, next.axis);
        centred(tft, line, 30, TFT_DARKGREY, 1);
        centred(tft, "BOOT button = again", 180, TFT_DARKGREY, 1);
        for (int i = 0; i < 4; i++) cross(tft, PX[i], PY[i], TFT_DARKGREY);
        button(tft, SAVE_X, "SAVE", TFT_DARKGREEN);
        button(tft, AGAIN_X, "AGAIN", TFT_NAVY);

        uint32_t idle = millis();
        bool again = false;
        while (!again) {
            if (bootDown(bootPin)) { again = true; break; }
            if (millis() - idle > IDLE_CANCEL_MS) {
                cal = old; apply(touch, cal);
                Serial.println("[touch] not saved (no tap for 60 s), old calibration kept");
                return false;
            }
            if (!touch.Pressed()) { delay(5); continue; }
            idle = millis();
            int x = touch.X(), y = touch.Y();
            bool onBtnRow = y >= BTN_Y - 4 && y < BTN_Y + BTN_H + 4;
            if (onBtnRow && x >= SAVE_X - 4 && x < SAVE_X + BTN_W + 4) {
                while (touch.Pressed()) delay(10);
                cal = next;
                Serial.println("[touch] calibration saved");
                return true;
            }
            if (onBtnRow && x >= AGAIN_X - 4 && x < AGAIN_X + BTN_W + 4) {
                while (touch.Pressed()) delay(10);
                again = true;
                break;
            }
            tft.fillCircle(x, y, 2, TFT_YELLOW);
        }
    }
}

}
