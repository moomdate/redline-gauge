// REDLINE — racing dash for the CYD (ESP32-2432S028R family)
// Copyright (c) 2026 moomdate. PolyForm Noncommercial 1.0.0 — see LICENSE.md
//
//   core 0 : data task  — active DataSource (simulator / serial / OBD-II BT / custom) + serial commands
//   core 1 : loop()     — touch, GaugeModel, partial-redraw renderer, settings page, LED + buzzer
//
// Touch (gauge):  SETUP button or status bar -> settings page
//                 hold main area   -> throttle, in SIM TOUCH (further right = more throttle)
//                 long-press main  -> reset peak RPM (other sources)
// Serial: mode=sim|touch|serial|obd|custom  theme=ice|lime|amber  shift=7000  bright=80
//         beep=on|off  peak=reset  help    — plus data lines in SERIAL mode.
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <TFT_Touch.h>
#include <driver/gpio.h>

#include "config.h"
#include "settings.h"
#include "data/gauge_bus.h"
#include "data/sim_source.h"
#include "data/serial_source.h"
#include "data/obd_source.h"
#include "data/custom_source.h"
#include "data/honda_source.h"
#include "ui/gauge_model.h"
#include "ui/gauge_ui.h"
#include "ui/settings_ui.h"
#include "ui/splash_ui.h"
#include "ui/theme.h"
#include "ui/drag_timer.h"
#include "ui/timer_ui.h"

static TFT_eSPI  tft;
//                    DCS DCLK DIN DOUT  (bit-banged XPT2046, proven for this board)
static TFT_Touch touch(33, 25, 32, 39);
static Preferences prefs;

// ---- data sources (order = SourceId in settings.h) -----------------------------------
static SimSource    simAuto(false);
static SimSource    simTouch(true);
static SerialSource serialSrc;
static ObdSource    obdSrc;
static CustomSource customSrc;
static HondaKSource hondaSrc;
static HondaKSource hondaTest(true);
static DataSource *const sources[SRC_COUNT] = { &simAuto, &simTouch, &serialSrc, &obdSrc, &customSrc, &hondaSrc, &hondaTest };

static volatile int requestedSrc = SRC_SIM_AUTO;
static volatile int activeSrc = -1;

// ---- settings --------------------------------------------------------------------------
static Settings settings;

static void loadSettings() {
    prefs.begin("gauge", false);
    settings.theme      = prefs.getUChar("theme", settings.theme);
    settings.source     = prefs.getUChar("src", settings.source);
    settings.shiftRpm   = prefs.getUShort("shift", settings.shiftRpm);
    settings.brightness = prefs.getUChar("bright", settings.brightness);
    settings.beep       = prefs.getBool("beep", settings.beep);
    settings.panels     = prefs.getUChar("panels", settings.panels);
    settings.gearMode   = prefs.getUChar("gearm", settings.gearMode);
    settings.invert     = prefs.getBool("invert", settings.invert);
    if (settings.panels >= PANELS_COUNT) settings.panels = PANELS_AUTO;
    if (settings.gearMode >= GEARMODE_COUNT) settings.gearMode = GEARMODE_AUTO;
    if (settings.theme >= THEME_COUNT) settings.theme = 0;
    if (settings.source >= SRC_COUNT) settings.source = SRC_SIM_AUTO;
    settings.shiftRpm = constrain(settings.shiftRpm, Settings::kShiftMin, Settings::kShiftMax);
    settings.brightness = constrain(settings.brightness, Settings::kBrightMin, 100);
}

static void saveSettings() {   // Preferences only writes keys whose value changed
    prefs.putUChar("theme", settings.theme);
    prefs.putUChar("src", settings.source);
    prefs.putUShort("shift", settings.shiftRpm);
    prefs.putUChar("bright", settings.brightness);
    prefs.putBool("beep", settings.beep);
    prefs.putUChar("panels", settings.panels);
    prefs.putUChar("gearm", settings.gearMode);
    prefs.putBool("invert", settings.invert);
}

static void backlightBegin() {
#if BACKLIGHT_DIMMING
    // Explicit channel 8 (low-speed group: its own timers, apart from the speaker on ch0).
    // ESP32 LEDC latches one duty update per PWM period: a second ledcWrite inside the
    // same 200 us is lost. Attaching writes duty 0 itself, so wait a period before the
    // caller's setBacklight() - otherwise that write is dropped and the screen stays dark.
    ledcAttachChannel(TFT_BL, 5000, 8, 8);           // after tft.init() has driven TFT_BL HIGH
    delayMicroseconds(500);
#else
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);                      // full on: this board can't dim with PWM
#endif
}

static void setBacklight(uint8_t pct) {
#if BACKLIGHT_DIMMING
    ledcWrite(TFT_BL, (uint32_t)pct * 255 / 100);
#else
    (void)pct;
#endif
}

// ---- serial commands --------------------------------------------------------------------------
// Read in loop() (core 1), NOT in the data task: the OBD source can sit in a blocking
// Bluetooth connect for 10+ s, and commands must still work meanwhile.
static void applySettings(const Settings &next, bool repaint = true);
static GaugeModel model;
static void printHelp() {
    Serial.println(F(
        "\n=== REDLINE " REDLINE_VERSION " — crafted by birdlab.th (birdlab.moomdate.tech) ===\n"
        "  mode=sim|touch|serial|obd|custom|honda|hondatest   theme=ice|lime|amber   shift=7000\n"
        "  bright=20..100   beep=on|off   peak=reset   help\n"
        "  panels=auto|standard|hybrid   gearmode=auto|off   invert=on|off (panel colours)\n"
        "  timer   timerlog   timerlog=clear   kdump (HONDA K raw tables)   klineinvert=on|off\n"
        "  obd=scan | obd=AA:BB:CC:DD:EE:FF   obdpin=1234|0000 (empty = auto)\n"
        "data (SERIAL mode):  rpm=3200 spd=86 clt=87 volt=13.9 iat=42 gear=3\n"
        "           or JSON:  {\"rpm\":3200,\"speed\":86,\"coolant\":87,\"voltage\":13.9}\n"));
}

static bool keyIs(const char *p, const char *key, const char **val) {
    size_t n = strlen(key);
    if (strncasecmp(p, key, n)) return false;
    p += n;
    if (*p != '=' && *p != ':' && *p != ' ') return false;
    while (*p == '=' || *p == ':' || *p == ' ') p++;
    *val = p;
    return true;
}

static void runBench();
static void openTimer();
static void printRunLog();
static void clearRunLog();

static void handleLine(char *line) {
    char *p = line;
    while (*p == ' ') p++;
    if (!*p) return;
    const char *v;

    Settings s = settings;
    bool changed = true;

    if (keyIs(p, "mode", &v)) {
        static const char *const keys[SRC_COUNT] = { "sim", "touch", "serial", "obd", "custom", "honda", "hondatest" };
        int found = -1;
        for (int i = 0; i < SRC_COUNT; i++) if (!strncasecmp(v, keys[i], strlen(keys[i]))) found = i;
        if (!strncasecmp(v, "auto", 4)) found = SRC_SIM_AUTO;
        if (!strncasecmp(v, "kline", 5)) found = SRC_HONDA;
        if (found < 0) { Serial.println("[gauge] unknown mode"); return; }
        s.source = found;
    } else if (keyIs(p, "theme", &v)) {
        int found = -1;
        for (int i = 0; i < THEME_COUNT; i++) if (!strncasecmp(v, kThemes[i].key, strlen(kThemes[i].key))) found = i;
        if (found < 0) { Serial.println("[gauge] themes: ice lime amber"); return; }
        s.theme = found;
    } else if (keyIs(p, "shift", &v)) {
        s.shiftRpm = constrain(atoi(v), Settings::kShiftMin, Settings::kShiftMax);
    } else if (keyIs(p, "bright", &v)) {
#if BACKLIGHT_DIMMING
        s.brightness = constrain(atoi(v), Settings::kBrightMin, 100);
#else
        Serial.println("[gauge] brightness is fixed: this board's backlight can't dim (BACKLIGHT_DIMMING 0)");
        return;
#endif
    } else if (keyIs(p, "panels", &v)) {
        if (!strncasecmp(v, "auto", 4)) s.panels = PANELS_AUTO;
        else if (!strncasecmp(v, "std", 3) || !strncasecmp(v, "standard", 8)) s.panels = PANELS_STANDARD;
        else if (!strncasecmp(v, "hyb", 3)) s.panels = PANELS_HYBRID;
        else { Serial.println("[gauge] panels=auto|standard|hybrid"); return; }
    } else if (keyIs(p, "invert", &v)) {
        s.invert = !strncasecmp(v, "on", 2) || *v == '1';
    } else if (keyIs(p, "gearmode", &v)) {           // not "gear=": that's a data key
        if (!strncasecmp(v, "auto", 4) || !strncasecmp(v, "on", 2)) s.gearMode = GEARMODE_AUTO;
        else if (!strncasecmp(v, "off", 3)) s.gearMode = GEARMODE_OFF;
        else { Serial.println("[gauge] gearmode=auto|off"); return; }
    } else if (keyIs(p, "beep", &v)) {
        s.beep = !strncasecmp(v, "on", 2) || *v == '1';
    } else if (keyIs(p, "obdpin", &v)) {
        char arg[32];
        snprintf(arg, sizeof arg, "pin=%s", v);
        ObdSource::command(arg);
        return;
    } else if (keyIs(p, "obd", &v)) {
        if (!ObdSource::command(v)) Serial.println("[gauge] obd=scan | obd=AA:BB:CC:DD:EE:FF | obdpin=1234");
        return;
    } else if (keyIs(p, "timerlog", &v)) {
        if (!strncasecmp(v, "clear", 5)) clearRunLog();
        else Serial.println("[gauge] timerlog | timerlog=clear");
        return;
    } else if (!strncasecmp(p, "timerlog", 8)) {
        printRunLog();
        return;
    } else if (!strncasecmp(p, "timer", 5)) {
        openTimer();
        return;
    } else if (keyIs(p, "klineinvert", &v)) {
        HondaKSource::setInvert(!strncasecmp(v, "on", 2) || *v == '1');
        Serial.printf("[gauge] K-line polarity %s (saved; applies on the next connect)\n",
                      HondaKSource::invert() ? "INVERTED (opto interface)" : "normal");
        return;
    } else if (!strncasecmp(p, "kdump", 5)) {
        HondaKSource::dumpOn = !HondaKSource::dumpOn;
        Serial.printf("[gauge] K-line table dump %s%s\n", HondaKSource::dumpOn ? "ON" : "OFF",
                      activeSrc == SRC_HONDA || activeSrc == SRC_HONDA_TEST ? "" : " (needs mode=honda)");
        return;
    } else if (!strncasecmp(p, "bench", 5)) {
        runBench();
        return;
    } else if (!strncasecmp(p, "peak", 4)) {
        model.resetPeaks();
        Serial.println("[gauge] ok  peak reset");
        return;
    } else if (!strncasecmp(p, "help", 4) || *p == '?') {
        printHelp();
        return;
    } else {
        changed = false;
    }

    if (changed) {
        applySettings(s);
        // confirm, so someone typing into `pio device monitor` sees it worked
        static const char *const panelKeys[PANELS_COUNT] = { "auto", "standard", "hybrid" };
        Serial.printf("[gauge] ok  theme=%s shift=%u beep=%s panels=%s gearmode=%s source=%s",
                      kThemes[settings.theme].key, settings.shiftRpm, settings.beep ? "on" : "off",
                      panelKeys[settings.panels], settings.gearMode == GEARMODE_OFF ? "off" : "auto",
                      sources[settings.source]->name());
#if BACKLIGHT_DIMMING
        Serial.printf(" bright=%u%%", settings.brightness);
#endif
        Serial.println();
        return;
    }
    int n = SerialSource::feed(p, activeSrc == SRC_SERIAL);
    // An external board may stream 20 lines/s: each hint prints at most every 5 s.
    static uint32_t lastIgnored = 0, lastUnknown = 0;
    uint32_t now = millis();
    if (n && activeSrc != SRC_SERIAL && (!lastIgnored || now - lastIgnored > 5000)) {
        Serial.println("[gauge] data ignored — switch to SERIAL first (mode=serial)");
        lastIgnored = now;
    } else if (!n && (!lastUnknown || now - lastUnknown > 5000)) {
        Serial.printf("[gauge] ? unknown: '%.40s' — type help\n", p);
        lastUnknown = now;
    }
}

// Collects one text line per input; USB and the external UART each keep their own
// buffer so interleaved bytes from the two can't corrupt each other.
struct LineReader {
    char   buf[160];
    size_t len = 0;
    void poll(Stream &in) {
        while (in.available()) {
            char c = (char)in.read();
            if (c == '\n' || c == '\r') {
                buf[len] = 0;
                if (len) handleLine(buf);
                len = 0;
            } else if (len < sizeof(buf) - 1) {
                buf[len++] = c;
            }
        }
    }
};

static void pollSerial() {
    static LineReader usb;
    usb.poll(Serial);
#if EXT_SERIAL_RX_PIN >= 0
    static LineReader ext;
    if (!HondaKSource::ownsUart) ext.poll(Serial2);  // HONDA K borrows the UART for K-line
#endif
}

static void dataTask(void *) {
    for (;;) {
        int req = requestedSrc;
        if (req != activeSrc) {
            if (activeSrc >= 0) sources[activeSrc]->end();
            bus::clear();
            Serial.printf("[gauge] source -> %s\n", sources[req]->name());
            sources[req]->begin();
            activeSrc = req;
        }
        sources[activeSrc]->poll();
        vTaskDelay(pdMS_TO_TICKS(4));
    }
}

// ---- outputs: RGB LED + speaker ---------------------------------------------------------
static uint32_t toneOffAt = 0;

// Speaker on LEDC channel 0, 10-bit. Frequency change and duty are separate single writes:
// ledcWriteTone() writes 50 % duty itself, and a second ledcWrite right after it falls in the
// same PWM period and is lost (the tone then played at full volume).
static void toneOn(uint32_t freq) {
    ledcChangeFrequency(PIN_SPEAKER, freq, 10);
    ledcWrite(PIN_SPEAKER, 512u * SPEAKER_VOLUME / 100);   // 512 = 50 % duty = loudest
}
static void toneOff() { ledcWrite(PIN_SPEAKER, 0); }

static void beep(uint32_t freq, uint32_t ms, bool force = false) {
    if (!settings.beep && !force) return;
    toneOn(freq);
    toneOffAt = millis() + ms;
}

// Touch feedback click: plays to the end BEFORE the screen redraws. A beep's current draw
// overlapping a big SPI push garbled the 80 MHz picture now and then (seen on a CYD with a
// PWM-dimmed backlight), so UI clicks never overlap drawing. Always plays, even with BEEP off.
static void click(uint32_t freq = 2400, uint32_t ms = 15) {
    toneOn(freq);
    delay(ms);
    toneOff();
    toneOffAt = 0;
}

static void setLed(bool r, bool g, bool b) {       // active LOW
    digitalWrite(PIN_LED_R, !r);
    digitalWrite(PIN_LED_G, !g);
    digitalWrite(PIN_LED_B, !b);
}

static void updateOutputs(const GaugeView *v) {
    static bool wasShift = false, wasCrit = false;
    bool shift = v && v->shift;
    bool crit = v && ((v->coolValid && v->coolLvl == LV_CRIT) || (v->voltValid && v->voltLvl == LV_CRIT) ||
                      (v->iatValid && v->iatLvl == LV_CRIT));

    if (shift)      setLed(v->shiftFlash, false, false);      // red strobe = shift now
    else if (crit)  setLed(v->blink, v->blink, false);         // amber blink = check a gauge
    else            setLed(false, false, false);

    if (shift && !wasShift) beep(3200, 70);
    if (crit && !wasCrit && !shift) beep(1100, 180);
    wasShift = shift;
    wasCrit = crit;

    if (toneOffAt && (int32_t)(millis() - toneOffAt) >= 0) {
        toneOff();
        toneOffAt = 0;
    }
}

// ---- screens ------------------------------------------------------------------------------------
enum Screen { SCR_GAUGE, SCR_SETTINGS, SCR_TIMER, SCR_LOG };
static Screen screen = SCR_GAUGE;

// ---- drag timer ---------------------------------------------------------------------------------
static DragTimer dragTimer;
static RunLog    runLog;

static void loadRunLog() {
    Preferences p;
    if (!p.begin("timer", false)) return;
    RunLog tmp;
    if (p.getBytesLength("log") == sizeof(RunLog) && p.getBytes("log", &tmp, sizeof tmp) == sizeof tmp &&
        tmp.version == runLog.version && tmp.count <= RunLog::kMax)
        runLog = tmp;
    p.end();
}

static void saveRunLog() {
    Preferences p;
    if (!p.begin("timer", false)) return;
    p.putBytes("log", &runLog, sizeof runLog);
    p.end();
}

static void printRunLog() {
    static const char *const names[SEG_COUNT] = { "0-100", "100-120", "120-160", "0-200", "160-200" };
    Serial.printf("run");
    for (const char *n : names) Serial.printf(",%s", n);
    Serial.println(",max_kmh,to_max_s");
    for (int i = runLog.count - 1; i >= 0; i--) {             // oldest first, like a spreadsheet
        const RunRecord &r = runLog.runs[i];
        Serial.printf("%u", r.seq);
        for (int s = 0; s < SEG_COUNT; s++)
            if (r.cs[s] == RUN_NONE) Serial.print(",");
            else Serial.printf(",%u.%02u", r.cs[s] / 100, r.cs[s] % 100);
        Serial.printf(",%u,%u.%02u\n", r.maxKmh, r.toMaxCs / 100, r.toMaxCs % 100);
    }
}

static void clearRunLog() {
    runLog.clear();
    saveRunLog();
    Serial.println("[gauge] run log cleared");
    if (screen == SCR_LOG) timer_ui::drawLog(runLog);
}

static void openTimer() {
    screen = SCR_TIMER;
    SimSource::touchThrottle = 0;
    dragTimer.again();
    ObdSource::fastSpeed = true;
    SimSource::dragMode = true;                    // SIM AUTO drives launches from a stop
    SimSource::restartDrag = true;                 // … starting at the line, even if it was mid-run
    gauge_ui::setTheme(kThemes[settings.theme]);   // repaint the art; timer_ui draws on it
    timer_ui::invalidate();
}

static void openRunLog() {
    screen = SCR_LOG;
    timer_ui::drawLog(runLog);
}

static void backToTimer() {
    screen = SCR_TIMER;
    gauge_ui::setTheme(kThemes[settings.theme]);
    timer_ui::invalidate();
}

// Apply `next` over the current settings; only what actually changed is touched.
// repaint=false when the settings page already redrew itself (tap on the page).
static void applySettings(const Settings &next, bool repaint) {
    Settings prev = settings;
    settings = next;
    if (next.source != prev.source) {
        requestedSrc = next.source;
        if (activeSrc == SRC_OBD) Serial.println("[gauge] switching after the Bluetooth attempt finishes…");
    }
    if (next.brightness != prev.brightness) setBacklight(next.brightness);
    if (next.invert != prev.invert) {
        tft.invertDisplay(next.invert);
        Serial.printf("[gauge] colour inversion %s\n", next.invert ? "ON" : "OFF");
    }
    model.setShiftRpm(next.shiftRpm);
    model.setPanels(next.panels);
    model.setGearHidden(next.gearMode == GEARMODE_OFF);
    SimSource::hybrid = next.panels == PANELS_HYBRID;
    if (screen == SCR_GAUGE) {
        if (next.theme != prev.theme) gauge_ui::setTheme(kThemes[next.theme]);
    } else if (repaint) {
        settings_ui::draw(settings);
    }
    saveSettings();
}

static void openSettings() {
    screen = SCR_SETTINGS;
    SimSource::touchThrottle = 0;
    settings_ui::draw(settings);
}

static void closeSettings() {
    screen = SCR_GAUGE;
    gauge_ui::setTheme(kThemes[settings.theme]);   // repaint background + all regions
}

// ---- touch ------------------------------------------------------------------------------------
static void handleTouch() {
    static bool down = false, longDone = false;
    static uint32_t downAt = 0;
    static int sx = 0, sy = 0;

    bool pressed = touch.Pressed();
    int x = pressed ? touch.X() : sx, y = pressed ? touch.Y() : sy;
    if (pressed && !down) { down = true; longDone = false; downAt = millis(); sx = x; sy = y; }

    if (screen == SCR_SETTINGS) {
        if (!pressed && down) {                  // act on release, at the press position
            down = false;
            Settings s = settings;
            switch (settings_ui::tap(sx, sy, s)) {
                case settings_ui::ACT_CHANGED:    click(); applySettings(s, false); break;
                case settings_ui::ACT_RESET_PEAK: click(1800, 40); model.resetPeaks(); break;
                case settings_ui::ACT_CLOSE:      click(); closeSettings(); break;
                case settings_ui::ACT_TIMER:      click(); openTimer(); break;
                default: break;
            }
        }
        return;
    }

    if (screen == SCR_TIMER) {
        if (activeSrc == SRC_SIM_TOUCH)                  // SIM TOUCH: the whole left side is the pedal
            SimSource::touchThrottle = (pressed && sx < 215 && sy > 26 && sy < 215)
                                       ? 0.25f + 0.75f * constrain(x, 0, 215) / 215.0f : 0.0f;
        if (!pressed && down) {
            down = false;
            switch (timer_ui::tapTimer(sx, sy, dragTimer)) {
                case timer_ui::ACT_EXIT:
                    click();
                    ObdSource::fastSpeed = false;
                    SimSource::dragMode = false;
                    SimSource::touchThrottle = 0;
                    closeSettings();                     // back to the gauge
                    break;
                case timer_ui::ACT_ABORT:
                case timer_ui::ACT_AGAIN: click(1800, 30); dragTimer.again(); SimSource::restartDrag = true; break;
                case timer_ui::ACT_LOG:   click(); openRunLog(); break;
                default: break;
            }
        }
        return;
    }

    if (screen == SCR_LOG) {
        if (!pressed && down) {
            down = false;
            switch (timer_ui::tapLog(sx, sy)) {
                case timer_ui::ACT_BACK:      click(); backToTimer(); break;
                case timer_ui::ACT_NEW_RUN:   click(); dragTimer.again(); SimSource::restartDrag = true; backToTimer(); break;
                case timer_ui::ACT_CLEAR_ASK: click(); timer_ui::drawLog(runLog); break;   // shows CONFIRM / CANCEL
                case timer_ui::ACT_CANCEL:    click(); timer_ui::drawLog(runLog); break;
                case timer_ui::ACT_CLEAR:     click(900, 120); clearRunLog(); break;       // redraws
                default: break;
            }
        }
        return;
    }

    if (activeSrc == SRC_SIM_TOUCH) {
        SimSource::touchThrottle = (pressed && gauge_ui::hitMain(sx, sy))
                                   ? 0.25f + 0.75f * constrain(x, 0, 215) / 215.0f : 0.0f;
    }

    if (pressed && !longDone && millis() - downAt > TOUCH_LONGPRESS_MS &&
        gauge_ui::hitMain(sx, sy) && activeSrc != SRC_SIM_TOUCH) {
        longDone = true;
        model.resetPeaks();
        click(1800, 40);
    }

    if (!pressed && down) {
        down = false;
        if (!longDone && millis() - downAt < 600 &&
            (gauge_ui::hitSetup(sx, sy) || gauge_ui::hitStatus(sx, sy))) {
            click();
            openSettings();
        }
    }
}

// ---- display ------------------------------------------------------------------------------------
static uint32_t g_pushUs = 0, g_pushPx = 0;   // bench counters

static void pushToTft(int x, int y, int w, int h, const uint16_t *px) {
    uint32_t t = micros();
    tft.pushImage(x, y, w, h, px);
    g_pushUs += micros() - t;
    g_pushPx += (uint32_t)w * h;
}

// `bench` over serial: render a fixed 0 -> 8000 -> 0 rpm sweep as fast as possible and
// report where the time goes. Same workload every run, so tuning changes are comparable.
static void runBench() {
    GaugeSnapshot snap;
    bus::snapshot(snap);
    GaugeView v;
    model.update(snap, millis(), "BENCH", v);
    v.rpmValid = v.speedValid = v.coolValid = v.voltValid = v.iatValid = true;
    v.gear = 3;
    gauge_ui::invalidate();
    gauge_ui::render(v);                               // full draw first, not measured
    const int N = 300;
    uint32_t worst = 0;
    g_pushUs = g_pushPx = 0;
    uint32_t t0 = micros();
    for (int i = 0; i < N; i++) {
        float ph = (float)i / N, tri = ph < 0.5f ? ph * 2 : (1 - ph) * 2;
        v.rpmBar = tri * RPM_MAX;
        v.rpmText = (int)(v.rpmBar / RPM_DISPLAY_STEP) * RPM_DISPLAY_STEP;
        v.peakMarker = 0;
        v.sessionPeak = RPM_MAX;
        v.shift = v.rpmBar >= RPM_SHIFT;
        v.shiftFlash = (i / 4) & 1;
        v.speed = (int)(tri * 180);
        v.coolant = 85 + (i / 30) % 5;
        v.iat = 40 + (i / 40) % 3;
        v.volt = 13.8f + ((i / 25) % 3) * 0.1f;
        uint32_t f0 = micros();
        gauge_ui::render(v);
        uint32_t d = micros() - f0;
        if (d > worst) worst = d;
    }
    uint32_t total = micros() - t0;
    Serial.printf("[bench] %d frames: avg %.2f ms (max %.2f) = %.0f fps possible | push %.2f ms + compose %.2f ms"
                  " per frame | %.1f KB/frame\n", N, total / 1000.0f / N, worst / 1000.0f, 1e6f * N / total,
                  g_pushUs / 1000.0f / N, (total - g_pushUs) / 1000.0f / N, g_pushPx * 2 / 1024.0f / N);
    gauge_ui::invalidate();                            // repaint the real values next frame
}

void setup() {
    Serial.begin(SERIAL_BAUD);
#if EXT_SERIAL_RX_PIN >= 0
    Serial2.begin(EXT_SERIAL_BAUD, SERIAL_8N1, EXT_SERIAL_RX_PIN, -1);   // RX only
    gpio_pullup_en((gpio_num_t)EXT_SERIAL_RX_PIN);   // idle-high when nothing is plugged in (no noise)
#endif
    pinMode(PIN_LED_R, OUTPUT);
    pinMode(PIN_LED_G, OUTPUT);
    pinMode(PIN_LED_B, OUTPUT);
    setLed(false, false, false);
    ledcAttachChannel(PIN_SPEAKER, 2000, 10, 0);    // silent until toneOn()

    loadSettings();
    loadRunLog();

    tft.init();
    tft.invertDisplay(settings.invert);              // saved choice beats the build default
    tft.setRotation(1);
    tft.setSwapBytes(true);                          // canvas holds plain RGB565
    backlightBegin();
    setBacklight(settings.brightness);
    touch.setCal(526, 3443, 750, 3377, 320, 240, 1); // proven values for this board

    // Boot splash with the birdlab.th credit (required by NOTICE); its mini shift
    // bar fills as a progress bar. A tap skips it, but only after kSkipAfterMs.
    splash_ui::draw(pushToTft, kThemes[settings.theme]);
    for (uint32_t t0 = millis(), t; (t = millis() - t0) < splash_ui::kDurationMs;) {
        splash_ui::progress(pushToTft, kThemes[settings.theme], (float)t / splash_ui::kDurationMs);
        if (t > splash_ui::kSkipAfterMs && touch.Pressed()) break;
        delay(16);
    }
    splash_ui::progress(pushToTft, kThemes[settings.theme], 1.0f);
    beep(2600, 25);                                  // short "ready" chirp (if beep is on)
    delay(25);
    toneOff();
    toneOffAt = 0;
    delay(155);
    while (touch.Pressed()) delay(10);               // don't let the skip-tap reach the gauge

    gauge_ui::begin(pushToTft, kThemes[settings.theme]);
    model.setShiftRpm(settings.shiftRpm);
    model.setPanels(settings.panels);
    model.setGearHidden(settings.gearMode == GEARMODE_OFF);
    SimSource::hybrid = settings.panels == PANELS_HYBRID;
    requestedSrc = settings.source;

    xTaskCreatePinnedToCore(dataTask, "data", 8192, nullptr, 2, nullptr, 0);
    printHelp();
    Serial.printf("[gauge] theme %s, source %s, shift %u, free heap %u\n", kThemes[settings.theme].name,
                  sources[settings.source]->name(), settings.shiftRpm, (unsigned)ESP.getFreeHeap());
}

void loop() {
    static GaugeView view;
    static int shownSrc = -2;
    static uint32_t nextFrame = 0, statAt = 0, frames = 0, pushes = 0;

    uint32_t now = millis();
    if ((int32_t)(now - nextFrame) < 0) { delay(1); return; }
    nextFrame = now + 1000 / UI_FPS;

    pollSerial();

    handleTouch();

    int src = activeSrc;
    if (src != shownSrc) { model.reset(now); shownSrc = src; }

    GaugeSnapshot snap;
    bus::snapshot(snap);
    now = millis();          // read AFTER the snapshot: no stamp in it can be newer than `now`
    model.update(snap, now, src >= 0 ? sources[src]->name() : "", view);

    if (screen == SCR_GAUGE) {
        gauge_ui::render(view);
        updateOutputs(&view);
        pushes += gauge_ui::lastPushedRegions();
    } else if (screen == SCR_TIMER) {
        // Timer sounds play even with BEEP off: they are the feedback this screen is for.
        static const char *const segNames[4] = { "0-100", "100-120", "120-160", "0-200" };
        static const Seg segIds[4] = { SEG_0_100, SEG_100_120, SEG_120_160, SEG_0_200 };
        static bool announced[4];                    // split boxes already printed this run
        auto printSplits = [&]() {
            for (int i = 0; i < 4; i++)
                if (!announced[i] && dragTimer.segDone(segIds[i])) {
                    announced[i] = true;
                    Serial.printf("[timer] %s stamped %.2f s\n", segNames[i], dragTimer.segTime(segIds[i], now));
                }
        };
        switch (dragTimer.update(snap.value[CH_SPEED], snap.stamp[CH_SPEED], now, runLog)) {
            case TE_ARMED: beep(1500, 40, true); Serial.println("[timer] READY"); break;
            case TE_START:
                beep(2200, 50, true);
                memset(announced, 0, sizeof announced);
                Serial.printf("[timer] START at %.1f km/h\n", snap.value[CH_SPEED]);
                break;
            case TE_SPLIT: beep(2800, 70, true); printSplits(); break;
            case TE_FINISH:
            case TE_SAVED: {
                bool best = false;
                for (int s = 0; s < SEG_COUNT; s++) best |= dragTimer.newBest(s);
                printSplits();                       // 200 stamps 0-200 in the same sample
                runLog.add(dragTimer.record());
                saveRunLog();
                beep(best ? 3400 : 2600, best ? 400 : 200, true);
                const RunRecord &r = runLog.runs[0];
                Serial.printf("[timer] run %u saved: top %u km/h in %u.%02u s, 0-100 %s, 0-200 %s%s\n", r.seq,
                              r.maxKmh, r.toMaxCs / 100, r.toMaxCs % 100,
                              r.cs[SEG_0_100] == RUN_NONE ? "--" : String(r.cs[SEG_0_100] / 100.0f, 2).c_str(),
                              r.cs[SEG_0_200] == RUN_NONE ? "--" : String(r.cs[SEG_0_200] / 100.0f, 2).c_str(),
                              best ? "  NEW BEST" : "");
                break;
            }
            case TE_DISCARD: beep(900, 150, true); Serial.printf("[timer] run discarded (top %.0f km/h)\n", dragTimer.maxKmh()); break;
            default: break;
        }
        timer_ui::render(dragTimer, runLog, now, view.speedValid);
        updateOutputs(nullptr);
    } else {
        updateOutputs(nullptr);                      // quiet while in settings
    }

    frames++;
    if (now - statAt > 10000) {
        Serial.printf("[gauge] %s  rpm=%d spd=%d clt=%d v=%.1f iat=%d  ui %.1f fps, %.1f regions/frame, heap %u\n",
                      src >= 0 ? sources[src]->name() : "-", view.rpmText, view.speed, view.coolant,
                      view.volt, view.iat, frames * 1000.0f / (now - statAt), (float)pushes / frames,
                      (unsigned)ESP.getFreeHeap());
        statAt = now; frames = 0; pushes = 0;
    }
}
