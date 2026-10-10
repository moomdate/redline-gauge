// Host unit tests — run with:  pio test -e native
// Everything here is the real firmware code, compiled for the host.
#include <unity.h>
#include <math.h>
#include <string.h>

#include "config.h"
#include "settings.h"
#include "data/gauge_bus.h"
#include "data/obd_parse.h"
#include "data/honda_kline.h"
#include "ui/touch_cal_math.h"
#include "data/serial_source.h"
#include "data/sim_source.h"
#include "ui/canvas.h"
#include "ui/gauge_model.h"
#include "ui/gauge_ui.h"
#include "ui/settings_ui.h"
#include "ui/splash_ui.h"
#include "fonts/font_title.h"
#include "assets/logo_birdlab.h"
#include "ui/shift_slots.h"
#include "ui/theme.h"
#include "ui/drag_timer.h"
#include "ui/timer_ui.h"
#include "fonts/font_timer.h"
#include "fonts/font_ready.h"
#include "fonts/font_label.h"
#include "fonts/font_rpm.h"
#include "fonts/font_small.h"
#include "fonts/font_speed.h"
#include "fonts/font_ui.h"
#include "fonts/font_value.h"

extern uint32_t g_host_ms;

// ---- fake display ----------------------------------------------------------------
static uint16_t fb[320 * 240];
static long pushedPx = 0;
static int pushes = 0;
static void push(int x, int y, int w, int h, const uint16_t *px) {
    for (int r = 0; r < h; r++) memcpy(&fb[(y + r) * 320 + x], px + r * w, w * 2);
    pushedPx += (long)w * h;
    pushes++;
}

void setUp() {}
void tearDown() {}

// ---- canvas / colour ----------------------------------------------------------------
static void test_rgb565_and_blend() {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, rgb565(0xFFFFFF));
    TEST_ASSERT_EQUAL_HEX16(0xF800, rgb565(0xFF0000));
    TEST_ASSERT_EQUAL_HEX16(0x07E0, rgb565(0x00FF00));
    TEST_ASSERT_EQUAL_HEX16(0x001F, rgb565(0x0000FF));
    TEST_ASSERT_EQUAL_HEX16(0x1234, blend565(0, 0xFFFF, 0x1234));
    TEST_ASSERT_EQUAL_HEX16(0xABCD, blend565(255, 0xABCD, 0x0000));
    uint16_t mid = blend565(128, 0xFFFF, 0x0000);
    TEST_ASSERT_INT_WITHIN(2, 15, mid >> 11);           // ~half red
}

static void test_canvas_clips_and_rejects_oversize() {
    static uint16_t buf[100];
    static uint16_t bg[320 * 240];
    Canvas cv(buf, 100);
    TEST_ASSERT_FALSE(cv.begin(0, 0, 20, 20, bg));      // 400 px > capacity
    TEST_ASSERT_TRUE(cv.begin(10, 10, 10, 10, bg));
    cv.fillRect(0, 0, 320, 240, 0xFFFF);                // way outside: must clip, not overflow
    for (int i = 0; i < 100; i++) TEST_ASSERT_EQUAL_HEX16(0xFFFF, buf[i]);
    cv.pixel(-5, 500, 0x0000);                          // out of range: ignored
}

static void test_tabular_digits_have_equal_width() {
    static uint16_t buf[16];
    Canvas cv(buf, 16);
    int w = cv.textWidth(font_rpm, "0000", 0, true);
    const char *samples[] = { "1111", "4444", "7777", "1234", "9876" };
    for (const char *s : samples) TEST_ASSERT_EQUAL_INT(w, cv.textWidth(font_rpm, s, 0, true));
}

// Every string the UI draws must exist in the font it's drawn with.
static bool fontHas(const GaugeFont &f, const char *s) {
    while (*s) {
        uint8_t c = (uint8_t)*s;
        uint16_t code = c;
        if (c >= 0xC0) { code = (uint16_t)(((c & 0x1F) << 6) | ((uint8_t)s[1] & 0x3F)); s++; }
        s++;
        bool found = false;
        for (int i = 0; i < f.count; i++) if (f.glyphs[i].code == code) found = true;
        if (!found) { printf("missing glyph U+%04X\n", code); return false; }
    }
    return true;
}

static void test_fonts_cover_every_ui_string() {
    TEST_ASSERT_TRUE(fontHas(font_rpm, "0123456789,-"));
    TEST_ASSERT_TRUE(fontHas(font_speed, "0123456789-N"));
    TEST_ASSERT_TRUE(fontHas(font_value, "0123456789.-"));
    TEST_ASSERT_TRUE(fontHas(font_label, "SPEED " PANEL1_LABEL PANEL2_LABEL PANEL3_LABEL));
    TEST_ASSERT_TRUE(fontHas(font_small, "ENGINE SPEED PEAK RPM km/h GEAR °C V SETUP " STATUS_TITLE
                                         "BEEP ON OFF RESET THEME DATA SOURCE SHIFT LIGHT BRIGHTNESS % kW"));
    for (int i = 0; i < PANELS_COUNT; i++) TEST_ASSERT_TRUE(fontHas(font_small, settings_ui::kPanelLabels[i]));
    TEST_ASSERT_TRUE(fontHas(font_small, "GEAR AUTO GEAR OFF"));
    TEST_ASSERT_TRUE(fontHas(font_label, HYBRID_PANEL2_LABEL HYBRID_PANEL3_LABEL));
    TEST_ASSERT_TRUE(fontHas(font_ui, "SETTINGS DONE + - 7,000 100%"));
    for (int t = 0; t < THEME_COUNT; t++) TEST_ASSERT_TRUE(fontHas(font_ui, kThemes[t].name));
    for (int i = 0; i < SRC_COUNT; i++) TEST_ASSERT_TRUE(fontHas(font_small, settings_ui::kSourceLabels[i]));
}

static void test_font_tables_sorted() {
    const GaugeFont *fonts[] = { &font_rpm, &font_speed, &font_value, &font_label, &font_small, &font_ui };
    for (const GaugeFont *f : fonts)
        for (int i = 1; i < f->count; i++) TEST_ASSERT_TRUE(f->glyphs[i - 1].code < f->glyphs[i].code);
}

// ---- theme assets ---------------------------------------------------------------------------
// The live shift-bar segments are drawn onto slots painted in the art. A new or
// re-processed background must keep those slots where SLOT_SPAN says they are.
static void test_theme_backgrounds_match_slot_geometry() {
    for (int t = 0; t < THEME_COUNT; t++) {
        const uint16_t *bg = kThemes[t].background;
        for (int r = 0; r < SLOT_ROWS; r += 4)
            for (int s = 0; s < SLOT_COUNT; s++) {
                int y = SLOT_Y0 + r, xm = (SLOT_SPAN[r][s][0] + SLOT_SPAN[r][s][1]) / 2;
                uint16_t c = bg[y * 320 + xm];
                int lum = ((c >> 11) << 3) + (((c >> 5) & 63) << 2) + ((c & 31) << 3);
                TEST_ASSERT_TRUE_MESSAGE(lum > 60, kThemes[t].name);   // slot interior is lit grey/red, not black
            }
    }
}

// ---- serial protocol --------------------------------------------------------------------------
static void test_serial_key_value() {
    bus::clear();
    TEST_ASSERT_EQUAL_INT(6, SerialSource::feed("rpm=3200 spd=86 clt=87 volt=13.9 iat=-5 gear=3", true));
    GaugeSnapshot s;
    bus::snapshot(s);
    TEST_ASSERT_EQUAL_FLOAT(3200, s.value[CH_RPM]);
    TEST_ASSERT_EQUAL_FLOAT(86, s.value[CH_SPEED]);
    TEST_ASSERT_EQUAL_FLOAT(-5, s.value[CH_IAT]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 13.9f, s.value[CH_VOLTAGE]);
}

static void test_serial_json_and_aliases() {
    bus::clear();
    TEST_ASSERT_EQUAL_INT(4, SerialSource::feed("{\"rpm\":4100,\"speed\":120.5,\"coolant\":99,\"voltage\":14.1}", true));
    TEST_ASSERT_EQUAL_INT(2, SerialSource::feed("RPM:900;V:12.2", true));
    GaugeSnapshot s;
    bus::snapshot(s);
    TEST_ASSERT_EQUAL_FLOAT(900, s.value[CH_RPM]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.5f, s.value[CH_SPEED]);
}

static void test_serial_ignores_garbage() {
    bus::clear();
    TEST_ASSERT_EQUAL_INT(0, SerialSource::feed("hello world", true));
    TEST_ASSERT_EQUAL_INT(0, SerialSource::feed("", true));
    TEST_ASSERT_EQUAL_INT(0, SerialSource::feed("foo=12 bar=3", true));
    TEST_ASSERT_EQUAL_INT(1, SerialSource::feed("rpm=100", false));   // counted, not published
    GaugeSnapshot s;
    bus::snapshot(s);
    TEST_ASSERT_EQUAL_UINT32(0, s.stamp[CH_RPM]);
}

// ---- OBD replies ---------------------------------------------------------------------------------
static void test_obd_parse() {
    float v = 0;
    TEST_ASSERT_EQUAL(OBD_OK, obdParsePid("410C1AF8", 0x0C, v));
    TEST_ASSERT_EQUAL_FLOAT(1726, v);                                   // (0x1A*256+0xF8)/4
    TEST_ASSERT_EQUAL(OBD_OK, obdParsePid("SEARCHING...410C0FA0", 0x0C, v));
    TEST_ASSERT_EQUAL_FLOAT(1000, v);
    TEST_ASSERT_EQUAL(OBD_OK, obdParsePid("410D56", 0x0D, v));
    TEST_ASSERT_EQUAL_FLOAT(86, v);
    TEST_ASSERT_EQUAL(OBD_OK, obdParsePid("41057B", 0x05, v));
    TEST_ASSERT_EQUAL_FLOAT(83, v);                                     // 0x7B - 40
    TEST_ASSERT_EQUAL(OBD_OK, obdParsePid("410F28", 0x0F, v));
    TEST_ASSERT_EQUAL_FLOAT(0, v);
    TEST_ASSERT_EQUAL(OBD_NODATA, obdParsePid("NODATA", 0x0C, v));
    TEST_ASSERT_EQUAL(OBD_BAD, obdParsePid("?", 0x0C, v));
    TEST_ASSERT_EQUAL(OBD_BAD, obdParsePid("410C1A", 0x0C, v));        // truncated
    TEST_ASSERT_EQUAL(OBD_BAD, obdParsePid("UNABLETOCONNECT", 0x0D, v));
    TEST_ASSERT_EQUAL(OBD_OK, obdParseVolt("13.9V", v));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 13.9f, v);
    TEST_ASSERT_EQUAL(OBD_BAD, obdParseVolt("?", v));
}

// ---- model ----------------------------------------------------------------------------------------
static void settle(GaugeModel &m, GaugeView &v, float rpm, float spd, float clt, float volt, float iat,
                   int frames = 60) {
    for (int i = 0; i < frames; i++) {
        g_host_ms += 33;
        bus::publish(CH_RPM, rpm);
        bus::publish(CH_SPEED, spd);
        bus::publish(CH_COOLANT, clt);
        bus::publish(CH_VOLTAGE, volt);
        bus::publish(CH_IAT, iat);
        GaugeSnapshot s;
        bus::snapshot(s);
        m.update(s, g_host_ms, "TEST", v);
    }
}

static void test_obd_adapter_selection() {
    TEST_ASSERT_TRUE(obdValidMac("AA:BB:CC:DD:EE:FF"));
    TEST_ASSERT_TRUE(obdValidMac("00:1d:a5:68:98:8b"));
    TEST_ASSERT_FALSE(obdValidMac("AA:BB:CC:DD:EE"));
    TEST_ASSERT_FALSE(obdValidMac("AA-BB-CC-DD-EE-FF"));
    TEST_ASSERT_FALSE(obdValidMac("GG:BB:CC:DD:EE:FF"));
    TEST_ASSERT_FALSE(obdValidMac(""));
    TEST_ASSERT_FALSE(obdValidMac(nullptr));
    // real ELM327 clone names
    const char *yes[] = {"OBDII", "OBD2", "obdII", "OBD-II", "V-LINK", "Android-Vlink", "VEEPEAK",
                         "KONNWEI", "Vgate iCar2", "OBDLink MX+", "ELM327 v1.5", "CARISTA", "KIWI 3"};
    for (const char *n : yes) TEST_ASSERT_TRUE_MESSAGE(obdNameLooksLikeAdapter(n, "OBDII"), n);
    // things that are nearby in a car or on a bike
    const char *no[] = {"", "HELMET-INTERCOM", "Cardo PACKTALK", "JBL Flip 5", "Galaxy S24",
                        "iPhone", "Toyota Touch", "SCANNER", "REDLINE"};
    for (const char *n : no) TEST_ASSERT_FALSE_MESSAGE(obdNameLooksLikeAdapter(n, "OBDII"), n);
    TEST_ASSERT_FALSE(obdNameLooksLikeAdapter(nullptr, "OBDII"));
    // a custom OBD_BT_NAME matches exactly, case-insensitive
    TEST_ASSERT_TRUE(obdNameLooksLikeAdapter("MyDongle", "mydongle"));
    TEST_ASSERT_FALSE(obdNameLooksLikeAdapter("MyDongle2", "mydongle"));
}

static void test_obd_supported_pid_mask() {
    // Typical CAN car: 0100 -> BE 3E B8 11 (has 05, 0C, 0D, 0F)
    uint32_t m = obdSupportedPids("4100BE3EB811");
    TEST_ASSERT_EQUAL_HEX32(0xBE3EB811, m);
    TEST_ASSERT_TRUE(obdPidSupported(m, 0x05));
    TEST_ASSERT_TRUE(obdPidSupported(m, 0x0C));
    TEST_ASSERT_TRUE(obdPidSupported(m, 0x0D));
    TEST_ASSERT_TRUE(obdPidSupported(m, 0x0F));
    TEST_ASSERT_FALSE(obdPidSupported(m, 0x02));
    TEST_ASSERT_FALSE(obdPidSupported(m, 0x00));
    TEST_ASSERT_FALSE(obdPidSupported(m, 0x21));
    // two ECUs answer, after the protocol search banner: masks are combined
    TEST_ASSERT_EQUAL_HEX32(0x98180001u | 0x00080000u,
                            obdSupportedPids("SEARCHING...4100981800014100000800 00"));
    TEST_ASSERT_EQUAL_HEX32(0, obdSupportedPids("NODATA"));
    TEST_ASSERT_EQUAL_HEX32(0, obdSupportedPids("4100BE3E"));   // truncated
}

static void test_obd_hybrid_pids() {
    float v, a;
    // Civic e:HEV capture: SOC 0x9D -> 61.6 %
    TEST_ASSERT_EQUAL(OBD_OK, obdParsePid("415B9D", 0x5B, v));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 61.6f, v);
    // 9A after frame-number stripping: A=1F B=07, 40C4, 050C -> 259.06 V, 129.2 A
    TEST_ASSERT_EQUAL(OBD_OK, obdParseHybrid("00B419A1F0740C4050C00000000", v, a));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 259.06f, v);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 129.2f, a);
    TEST_ASSERT_EQUAL(OBD_OK, obdParseHybrid("419A1F0740C4FF38", v, a));        // regen: -20.0 A
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f, a);
    TEST_ASSERT_EQUAL(OBD_NODATA, obdParseHybrid("NODATA", v, a));
    TEST_ASSERT_EQUAL(OBD_BAD, obdParseHybrid("419A0740", v, a));             // truncated
    // one ECU answers, another says NO DATA: the data wins
    TEST_ASSERT_EQUAL(OBD_OK, obdParsePid("NODATA410C1AF8", 0x0C, v));
    // supported-PID pages: 5B lives on page 0x40, 9A on 0x80
    uint32_t p40 = obdSupportedPidsPage("4140FED08420", 0x40);
    TEST_ASSERT_EQUAL_HEX32(0xFED08420, p40);
    TEST_ASSERT_TRUE(obdPidSupported(0x00000020u, 0x5B, 0x40));
    TEST_ASSERT_FALSE(obdPidSupported(0x00000020u, 0x5A, 0x40));
    TEST_ASSERT_TRUE(obdPidSupported(0x00000040u, 0x9A, 0x80));
    TEST_ASSERT_FALSE(obdPidSupported(0xFFFFFFFFu, 0x9A, 0x40));              // wrong page
}

static void test_model_hybrid_panels_and_gear_off() {
    GaugeModel m;
    GaugeSnapshot s = {};
    GaugeView v;
    s.link = Link::Live;
    g_host_ms = 10000;
    m.reset(g_host_ms);
    s.value[CH_RPM] = 2000; s.stamp[CH_RPM] = g_host_ms;
    s.value[CH_SPEED] = 60; s.stamp[CH_SPEED] = g_host_ms;
    m.update(s, g_host_ms, "OBD", v);
    TEST_ASSERT_FALSE(v.hybrid);                       // AUTO, no hybrid data yet
    TEST_ASSERT_TRUE(v.gear >= 0);
    s.value[CH_HV_SOC] = 61; s.stamp[CH_HV_SOC] = g_host_ms;
    m.update(s, g_host_ms, "OBD", v);
    TEST_ASSERT_TRUE(v.hybrid);                        // AUTO switches on hybrid data
    TEST_ASSERT_EQUAL(61, v.soc);
    g_host_ms += 10000;                                // data goes stale: stays hybrid (latched)
    m.update(s, g_host_ms, "OBD", v);
    TEST_ASSERT_TRUE(v.hybrid);
    TEST_ASSERT_FALSE(v.socValid);
    m.reset(g_host_ms);                                // new source: latch cleared
    m.update(s, g_host_ms, "OBD", v);
    TEST_ASSERT_FALSE(v.hybrid);
    m.setPanels(PANELS_HYBRID);
    m.update(s, g_host_ms, "OBD", v);
    TEST_ASSERT_TRUE(v.hybrid);
    m.setPanels(PANELS_STANDARD);
    s.stamp[CH_HV_SOC] = g_host_ms;
    m.update(s, g_host_ms, "OBD", v);
    TEST_ASSERT_FALSE(v.hybrid);                       // STANDARD ignores hybrid data
    m.setGearHidden(true);
    s.stamp[CH_RPM] = s.stamp[CH_SPEED] = g_host_ms;
    m.update(s, g_host_ms, "OBD", v);
    TEST_ASSERT_EQUAL(-1, v.gear);
    s.value[CH_GEAR] = 3; s.stamp[CH_GEAR] = g_host_ms;
    m.update(s, g_host_ms, "OBD", v);
    TEST_ASSERT_EQUAL(-1, v.gear);                     // even a sent gear stays hidden
}

// ---- drag timer ------------------------------------------------------------------------------
// Feeds a speed profile sampled every `dtMs` (rounded to whole km/h like OBD) and runs it.
struct DragSim {
    DragTimer t; RunLog log;
    uint32_t ms = 50000;
    TimerEvent feed(float kmh, uint32_t dtMs = 200, bool round = true) {
        ms += dtMs;
        return t.update(round ? floorf(kmh) : kmh, ms, ms, log);
    }
};

static void test_drag_timer_constant_accel() {
    DragSim d;
    d.t.again();
    for (int i = 0; i < 8; i++) d.feed(0);                          // 1.6 s stopped -> armed
    TEST_ASSERT_EQUAL(TS_STAGED, d.t.state());
    // launch 70 ms after the last zero sample, 10 km/h per s, then 10 km/h/s to 210
    uint32_t launch = d.ms + 70;
    TimerEvent last = TE_NONE;
    int splits = 0;
    while (d.t.state() != TS_FINISH && d.ms < launch + 40000) {
        uint32_t next = d.ms + 200;
        float v = next > launch ? (next - launch) / 1000.0f * 10.0f : 0;
        last = d.feed(v);
        if (last == TE_SPLIT) splits++;
    }
    TEST_ASSERT_EQUAL(TS_FINISH, d.t.state());
    TEST_ASSERT_EQUAL(TE_FINISH, last);
    TEST_ASSERT_EQUAL(3, splits);
    const RunRecord &r = d.t.record();
    TEST_ASSERT_UINT16_WITHIN(15, 1000, r.cs[SEG_0_100]);            // 10.00 s, within 0.15 s
    TEST_ASSERT_UINT16_WITHIN(10, 200, r.cs[SEG_100_120]);
    TEST_ASSERT_UINT16_WITHIN(10, 400, r.cs[SEG_120_160]);
    TEST_ASSERT_UINT16_WITHIN(15, 2000, r.cs[SEG_0_200]);
    TEST_ASSERT_UINT16_WITHIN(10, 400, r.cs[SEG_160_200]);
    for (int s = 0; s < SEG_COUNT; s++) TEST_ASSERT_TRUE(d.t.newBest(s));   // empty log: all best
}

static void test_drag_timer_lift_and_arming() {
    DragSim d;
    d.t.again();
    d.feed(30);
    d.feed(30);
    TEST_ASSERT_EQUAL(TS_MOVING, d.t.state());                      // rolling: not ready
    TEST_ASSERT_EQUAL(TE_ARMED, d.feed(0));                         // stopped: ready at once
    TEST_ASSERT_EQUAL(TS_STAGED, d.t.state());
    // run to 140, then lift: saved with 0-100 / 100-120 / 120-160? no: 160 not reached
    float v = 0;
    while (v < 140) { v += 3; d.feed(v); }
    TEST_ASSERT_EQUAL(TE_SAVED, d.feed(125));
    TEST_ASSERT_EQUAL(TS_SAVED, d.t.state());
    TEST_ASSERT_NOT_EQUAL(RUN_NONE, d.t.record().cs[SEG_0_100]);
    TEST_ASSERT_NOT_EQUAL(RUN_NONE, d.t.record().cs[SEG_100_120]);
    TEST_ASSERT_EQUAL_UINT16(RUN_NONE, d.t.record().cs[SEG_120_160]);
    TEST_ASSERT_EQUAL_UINT16(RUN_NONE, d.t.record().cs[SEG_0_200]);
    TEST_ASSERT_EQUAL_UINT16(141, d.t.record().maxKmh);
    // stop for 3 s: armed again
    for (int i = 0; i < 16; i++) d.feed(0);
    TEST_ASSERT_EQUAL(TS_STAGED, d.t.state());
    // short run that never reaches 100: still logged with its top speed
    v = 0;
    while (v < 60) { v += 4; d.feed(v); }
    TEST_ASSERT_EQUAL(TE_SAVED, d.feed(40));
    TEST_ASSERT_EQUAL(TS_SAVED, d.t.state());
    TEST_ASSERT_EQUAL_UINT16(60, d.t.record().maxKmh);
    TEST_ASSERT_UINT16_WITHIN(5, 300, d.t.record().toMaxCs);        // 15 samples x 0.2 s
    TEST_ASSERT_EQUAL_UINT16(RUN_NONE, d.t.record().cs[SEG_0_100]);
    // creeping forward under 30 km/h: not logged
    for (int i = 0; i < 16; i++) d.feed(0);
    d.feed(10); d.feed(20);
    TEST_ASSERT_EQUAL(TE_DISCARD, d.feed(5));
    TEST_ASSERT_EQUAL(TS_NO_RESULT, d.t.state());
    // speed data disappears mid-run: run ends
    for (int i = 0; i < 16; i++) d.feed(0);
    v = 0;
    while (v < 110) { v += 5; d.feed(v); }
    d.ms += DATA_STALE_MS + 100;
    TEST_ASSERT_EQUAL(TE_SAVED, d.t.update(110, d.ms - DATA_STALE_MS - 100, d.ms, d.log));
}

static void test_drag_timer_throttle_start() {
    DragSim d;
    d.t.again();
    d.feed(0);
    TEST_ASSERT_EQUAL(TS_STAGED, d.t.state());
    // throttle pressed at t0, the car only starts moving 300 ms later: the clock runs from t0
    d.ms += 50;
    uint32_t t0 = d.ms;
    TEST_ASSERT_EQUAL(TE_NONE, d.t.throttle(5, d.ms, d.ms));        // under 10 %: not a press
    d.ms += 10;
    t0 = d.ms;
    TEST_ASSERT_EQUAL(TE_START, d.t.throttle(80, d.ms, d.ms));
    TEST_ASSERT_EQUAL(TS_RUN, d.t.state());
    d.feed(0, 100); d.feed(0, 100); d.feed(0, 100);                 // still 0 for 300 ms: run stays
    TEST_ASSERT_EQUAL(TS_RUN, d.t.state());
    float v = 0;                                                     // then 20 km/h/s
    while (d.t.state() == TS_RUN && v < 140) { v += 2; d.feed(v, 100, false); }
    for (int i = 0; i < 3; i++) d.feed(v - 20);                      // lift
    TEST_ASSERT_EQUAL(TS_SAVED, d.t.state());
    // 0-100 = 0.3 s standing + 5.0 s at 20 km/h/s, counted from the throttle press
    TEST_ASSERT_UINT16_WITHIN(5, 530, d.t.record().cs[SEG_0_100]);
    (void)t0;
    // false start: throttle blip, car never moves -> back to READY, nothing logged
    for (int i = 0; i < 3; i++) d.feed(0);
    TEST_ASSERT_EQUAL(TS_STAGED, d.t.state());
    d.ms += 10;
    d.t.throttle(0, d.ms, d.ms);
    d.ms += 10;
    TEST_ASSERT_EQUAL(TE_START, d.t.throttle(60, d.ms, d.ms));
    for (int i = 0; i < 20; i++) d.feed(0);                          // 4 s, never moves
    TEST_ASSERT_EQUAL(TS_STAGED, d.t.state());
}

static void test_run_log() {
    RunLog log;
    log.clear();
    RunRecord r = {};
    for (int s = 0; s < SEG_COUNT; s++) r.cs[s] = RUN_NONE;
    TEST_ASSERT_EQUAL_UINT16(RUN_NONE, log.best(SEG_0_100));
    for (int i = 0; i < 25; i++) { r.cs[SEG_0_100] = 900 - i; log.add(r); }
    TEST_ASSERT_EQUAL(RunLog::kMax, log.count);
    TEST_ASSERT_EQUAL_UINT16(25, log.runs[0].seq);                  // newest first
    TEST_ASSERT_EQUAL_UINT16(876, log.best(SEG_0_100));
    TEST_ASSERT_EQUAL_UINT16(RUN_NONE, log.best(SEG_0_200));
    log.clear();
    TEST_ASSERT_EQUAL(0, log.count);
    log.add(r);
    TEST_ASSERT_EQUAL_UINT16(1, log.runs[0].seq);
}

static void test_timer_ui_renders_every_state() {
    gauge_ui::begin(push, kThemes[0]);
    TEST_ASSERT_TRUE(fontHas(font_timer, "0123456789.-"));
    TEST_ASSERT_TRUE(fontHas(font_label, "0-100 100-120 120-160 SPEED"));
    DragSim d;
    d.t.again();
    timer_ui::invalidate();
    timer_ui::render(d.t, d.log, d.ms, false);                      // NO SPEED
    for (int i = 0; i < 8; i++) d.feed(0);
    timer_ui::render(d.t, d.log, d.ms, true);                       // STAGED
    float v = 0;
    while (d.t.state() != TS_FINISH) {
        v += 2.5f;
        d.feed(v, 100, false);
        timer_ui::render(d.t, d.log, d.ms, true);                   // RUN, every split
    }
    d.log.add(d.t.record());
    timer_ui::render(d.t, d.log, d.ms, true);                       // FINISH
    timer_ui::drawLog(d.log);
    TEST_ASSERT_TRUE(fontHas(font_ready, "READY!!"));
    TEST_ASSERT_EQUAL(timer_ui::ACT_BACK, timer_ui::tapLog(280, 14));
    // clearing needs CLEAR then CONFIRM; CANCEL backs out
    TEST_ASSERT_EQUAL(timer_ui::ACT_CLEAR_ASK, timer_ui::tapLog(60, 224));
    timer_ui::drawLog(d.log);
    TEST_ASSERT_EQUAL(timer_ui::ACT_CANCEL, timer_ui::tapLog(238, 224));
    TEST_ASSERT_EQUAL(timer_ui::ACT_CLEAR_ASK, timer_ui::tapLog(60, 224));
    TEST_ASSERT_EQUAL(timer_ui::ACT_CLEAR, timer_ui::tapLog(60, 224));
    TEST_ASSERT_EQUAL(timer_ui::ACT_NEW_RUN, timer_ui::tapLog(238, 224));
    TEST_ASSERT_EQUAL(timer_ui::ACT_AGAIN, timer_ui::tapTimer(288, 229, d.t));
    TEST_ASSERT_EQUAL(timer_ui::ACT_LOG, timer_ui::tapTimer(230, 229, d.t));
}

// The owner's flow: open -> READY at once -> clock starts when the speed leaves 0 -> every
// threshold stamps its box (TE_SPLIT) -> 200 finishes -> the run is logged -> stop -> READY.
static void test_drag_timer_owner_flow() {
    DragSim d;
    d.t.again();
    TEST_ASSERT_EQUAL(TE_ARMED, d.feed(0));                         // first stopped sample: ready
    TEST_ASSERT_EQUAL(TS_STAGED, d.t.state());
    TEST_ASSERT_EQUAL(TE_NONE, d.feed(0));
    TEST_ASSERT_EQUAL(TE_START, d.feed(2, 100, false));             // speed leaves 0: clock starts
    TEST_ASSERT_TRUE(d.t.running());
    TEST_ASSERT_TRUE(d.t.segActive(SEG_0_100));
    TEST_ASSERT_TRUE(d.t.segActive(SEG_0_200));
    TEST_ASSERT_FALSE(d.t.segDone(SEG_0_100));
    TEST_ASSERT_TRUE(d.t.segAge(SEG_0_100, d.ms) < 0);
    float v = 2;
    int splits = 0;
    TimerEvent ev = TE_NONE;
    while (ev != TE_FINISH) {
        v += 2;                                                      // 20 km/h per s
        ev = d.feed(v, 100, false);
        if (ev == TE_SPLIT) {
            splits++;
            if (splits == 1) {                                       // 100: its box is stamped now
                TEST_ASSERT_TRUE(d.t.segDone(SEG_0_100));
                TEST_ASSERT_FLOAT_WITHIN(0.01f, 0, d.t.segAge(SEG_0_100, d.ms));
                TEST_ASSERT_FLOAT_WITHIN(0.06f, 5.0f, d.t.segTime(SEG_0_100, d.ms));   // launch back-estimated to the last 0 sample
                TEST_ASSERT_TRUE(d.t.segActive(SEG_100_120));        // next box being chased
            }
            if (splits == 2) TEST_ASSERT_TRUE(d.t.segDone(SEG_100_120));
            if (splits == 3) TEST_ASSERT_TRUE(d.t.segDone(SEG_120_160));
        }
    }
    TEST_ASSERT_EQUAL(3, splits);
    TEST_ASSERT_EQUAL(TS_FINISH, d.t.state());
    TEST_ASSERT_TRUE(d.t.segDone(SEG_0_200));
    TEST_ASSERT_UINT16_WITHIN(6, 1000, d.t.record().cs[SEG_0_200]);
    TEST_ASSERT_UINT16_WITHIN(5, 100, d.t.record().cs[SEG_100_120]);
    d.log.add(d.t.record());
    TEST_ASSERT_EQUAL(1, d.log.count);
    TEST_ASSERT_EQUAL_UINT16(200, d.log.runs[0].maxKmh);
    for (int i = 0; i < 3; i++) d.feed(v -= 60);                     // braking
    TEST_ASSERT_EQUAL(TS_FINISH, d.t.state());                      // result stays up
    TEST_ASSERT_EQUAL(TE_ARMED, d.feed(0));                         // stopped: ready again
    TEST_ASSERT_EQUAL(TS_STAGED, d.t.state());
}

// A sample published on the data core a few ms AFTER the UI read its clock has a stamp
// ahead of `now`. That is fresh data: it must not end the run (it did: "discarded, top 0")
// nor blank the value for a frame (the flicker).
static void test_stamp_ahead_of_now_is_fresh() {
    DragSim d;
    d.t.again();
    d.feed(0);
    d.feed(5, 100, false);
    TEST_ASSERT_TRUE(d.t.running());
    d.ms += 40;
    TEST_ASSERT_EQUAL(TE_NONE, d.t.update(8, d.ms + 3, d.ms, d.log));    // stamp 3 ms ahead
    TEST_ASSERT_TRUE(d.t.running());
    TEST_ASSERT_EQUAL(TE_NONE, d.t.update(8, d.ms + 3, d.ms + 10, d.log)); // same sample again
    d.ms += 40;
    TEST_ASSERT_EQUAL(TE_NONE, d.t.update(12, d.ms, d.ms, d.log));
    TEST_ASSERT_TRUE(d.t.running());
    // while READY the same glitch must not re-arm (it beeped READY twice)
    DragSim e;
    e.t.again();
    TEST_ASSERT_EQUAL(TE_ARMED, e.feed(0));
    e.ms += 40;
    TEST_ASSERT_EQUAL(TE_NONE, e.t.update(0, e.ms + 2, e.ms, e.log));
    TEST_ASSERT_EQUAL(TS_STAGED, e.t.state());
    // the gauge model: a stamp ahead of now is valid data
    GaugeModel m; GaugeView v;
    GaugeSnapshot s = {};
    g_host_ms = 90000;
    m.reset(g_host_ms);
    s.value[CH_SPEED] = 42; s.stamp[CH_SPEED] = g_host_ms + 2;
    s.value[CH_RPM] = 3000; s.stamp[CH_RPM] = g_host_ms + 2;
    m.update(s, g_host_ms, "T", v);
    TEST_ASSERT_TRUE(v.speedValid);
    TEST_ASSERT_TRUE(v.rpmValid);
    s.stamp[CH_SPEED] = g_host_ms - DATA_STALE_MS - 1;
    m.update(s, g_host_ms, "T", v);
    TEST_ASSERT_FALSE(v.speedValid);
}

// SIM AUTO with the timer open, driven exactly like main.cpp (sim polled on its own clock,
// the UI samples the bus at 60 fps): the car is put at the line at a dead stop, launches,
// every split is stamped, 200 finishes the run; a restart mid-run puts it back at the line.
static void test_sim_drag_launch_from_line() {
    SimSource sim(false);
    DragTimer t; RunLog log; log.clear();
    g_host_ms = 1000;
    sim.begin();
    for (int i = 0; i < 2000; i++) { g_host_ms += 10; sim.poll(); }   // street loop for 20 s
    GaugeSnapshot s;
    bus::snapshot(s);
    TEST_ASSERT_TRUE(s.value[CH_SPEED] > 20);                        // mid-drive when opened
    t.again();
    SimSource::dragMode = true;
    SimSource::restartDrag = true;
    uint32_t opened = g_host_ms, started = 0, finished = 0;
    int splits = 0, zeroSamples = 0;
    bool wasReady = false;
    auto frame = [&]() {                                             // one UI frame
        bus::snapshot(s);
        return t.update(s.value[CH_SPEED], s.stamp[CH_SPEED], g_host_ms, log);
    };
    for (int i = 0; i < 6000 && !finished; i++) {
        g_host_ms += 16;
        sim.poll();
        TimerEvent ev = frame();
        if (t.state() == TS_STAGED) { wasReady = true; if (s.value[CH_SPEED] == 0) zeroSamples++; }
        if (ev == TE_START) started = g_host_ms;
        if (ev == TE_SPLIT) splits++;
        if (ev == TE_FINISH) finished = g_host_ms;
        if (ev == TE_SAVED || ev == TE_DISCARD) TEST_FAIL_MESSAGE("run ended early");
    }
    TEST_ASSERT_TRUE(wasReady);
    TEST_ASSERT_TRUE(zeroSamples > 100);                             // parked at exactly 0 km/h
    TEST_ASSERT_TRUE(started && started - opened < 6000);            // launches within 6 s
    TEST_ASSERT_TRUE(finished);
    TEST_ASSERT_EQUAL(3, splits);
    const RunRecord &r = t.record();
    TEST_ASSERT_TRUE(r.cs[SEG_0_100] > 500 && r.cs[SEG_0_100] < 900);   // 5-9 s: a 2.0 turbo
    TEST_ASSERT_TRUE(r.cs[SEG_0_200] > 1500 && r.cs[SEG_0_200] < 3500);
    TEST_ASSERT_EQUAL_UINT16(200, r.maxKmh);
    log.add(r);
    // the next run starts by itself: the sim brakes to a stop, idles, launches again
    bool readyAgain = false, ranAgain = false;
    for (int i = 0; i < 4000 && !ranAgain; i++) {
        g_host_ms += 16;
        sim.poll();
        TimerEvent ev = frame();
        if (ev == TE_ARMED) readyAgain = true;
        if (ev == TE_START) ranAgain = true;
    }
    TEST_ASSERT_TRUE(readyAgain);
    TEST_ASSERT_TRUE(ranAgain);
    // AGAIN mid-run: the car is back at the line within a few samples and a full run follows
    for (int i = 0; i < 300; i++) { g_host_ms += 16; sim.poll(); frame(); }   // ~5 s into the run
    TEST_ASSERT_TRUE(t.running());
    t.again();
    SimSource::restartDrag = true;
    bool ready = false;
    for (int i = 0; i < 20 && !ready; i++) { g_host_ms += 16; sim.poll(); ready = frame() == TE_ARMED; }
    TEST_ASSERT_TRUE(ready);
    finished = 0;
    for (int i = 0; i < 4000 && !finished; i++) { g_host_ms += 16; sim.poll(); if (frame() == TE_FINISH) finished = 1; }
    TEST_ASSERT_TRUE(finished);
    SimSource::dragMode = false;
    SimSource::restartDrag = false;
}

// The timer screen pushes only what changed: a speed digit, the clock's last digits, the
// moving edge of the bar, a progress strip. Whole-region pushes at 60 fps tore on the panel.
static void test_timer_ui_partial_pushes() {
    gauge_ui::begin(push, kThemes[0]);
    DragSim d;
    d.t.again();
    d.feed(0);
    timer_ui::invalidate();
    timer_ui::render(d.t, d.log, d.ms, true);                       // READY, full paint
    float v = 0;
    while (v < 90) { v += 3; d.feed(v, 100, false); }
    timer_ui::render(d.t, d.log, d.ms, true);
    uint16_t full[320 * 240];
    // a frame where only the clock ticked: just a digit cell or two moves
    pushedPx = 0;
    timer_ui::render(d.t, d.log, d.ms + 10, true);
    TEST_ASSERT_TRUE_MESSAGE(pushedPx > 0 && pushedPx < 60 * 28, "clock tick pushed too much");
    // speed 90 -> 91: one big digit cell (plus the clock / bar / strips), far under the big region
    d.feed(91, 100, false);
    pushedPx = 0;
    timer_ui::render(d.t, d.log, d.ms, true);
    TEST_ASSERT_TRUE_MESSAGE(pushedPx < 196 * 51 / 2, "speed digit pushed the whole region");
    // and the partial frames are pixel-identical to a full repaint
    memcpy(full, fb, sizeof fb);
    timer_ui::invalidate();
    timer_ui::render(d.t, d.log, d.ms, true);
    TEST_ASSERT_EQUAL_MEMORY(full, fb, sizeof fb);
    // a split stamps its box: a full box repaint, then the strip only
    while (d.feed(v += 3, 100, false) != TE_SPLIT) {}
    timer_ui::render(d.t, d.log, d.ms, true);
    memcpy(full, fb, sizeof fb);
    timer_ui::invalidate();
    timer_ui::render(d.t, d.log, d.ms, true);
    TEST_ASSERT_EQUAL_MEMORY(full, fb, sizeof fb);
    TEST_ASSERT_TRUE(fontHas(font_small, "LAUNCH TO START NEXT 100 KM/H BEST 0-200 STOP TO ARM TOO SHORT"));
    TEST_ASSERT_TRUE(fontHas(font_label, "0-200 KM/H TIME 0-144"));
}

// ---- Honda K-line ------------------------------------------------------------------------------
static void test_honda_kline_frames() {
    // fixed frames from the protocol references must checksum to zero
    TEST_ASSERT_EQUAL_HEX8(0x8C, hkChecksum(HK_PING, 3));
    TEST_ASSERT_EQUAL_HEX8(0x99, hkChecksum(HK_INIT, 4));
    uint8_t req[5];
    hkTableRequest(HK_TABLE, req);
    const uint8_t want17[] = { 0x72, 0x05, 0x71, 0x17, 0x01 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want17, req, 5);
    hkTableRequest(0x20, req);
    TEST_ASSERT_EQUAL_HEX8(0xF8, req[4]);
    const uint8_t pingReply[] = { 0x0E, 0x04, 0x72, 0x7C };
    TEST_ASSERT_TRUE(hkFrameOk(pingReply, 4));

    // table 0x17 (Wave): 1500 rpm, TPS 0x3C*0.5 = 30 %, engine d[5] 0x82-40 = 90 C,
    // intake d[7] 0x48-40 = 32 C, battery 0x7D = 12.5 V
    uint8_t g[19] = { 0x02, 0x13, 0x71, 0x17, 0x05, 0xDC, 0x33, 0x3C, 0, 0x82, 0, 0x48, 0, 0,
                      0x7D, 0x01, 0x20, 0x90, 0 };
    g[18] = hkChecksum(g, 18);
    HondaData d;
    TEST_ASSERT_TRUE(hkDecodeMain(g, sizeof g, d));
    TEST_ASSERT_EQUAL_FLOAT(1500, d.rpm);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30, d.tps);
    TEST_ASSERT_EQUAL_FLOAT(90, d.temp);
    TEST_ASSERT_EQUAL_FLOAT(32, d.iat);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.5f, d.batt);
    g[7] = 0xFF; g[18] = hkChecksum(g, 18);
    TEST_ASSERT_TRUE(hkDecodeMain(g, sizeof g, d));
    TEST_ASSERT_EQUAL_FLOAT(100, d.tps);                         // clamped
    g[3] = 0x11; g[18] = hkChecksum(g, 18);
    TEST_ASSERT_FALSE(hkDecodeMain(g, sizeof g, d));             // wrong table id
    g[3] = 0x17; g[18] = hkChecksum(g, 18); g[9] ^= 1;
    TEST_ASSERT_FALSE(hkDecodeMain(g, sizeof g, d));             // bad checksum
    uint8_t s[] = { 0x02, 0x08, 0x71, 0x17, 0x05, 0xDC, 0x33, 0 };   // too short
    s[7] = hkChecksum(s, 7);
    TEST_ASSERT_FALSE(hkDecodeMain(s, sizeof s, d));
}

// ---- touch calibration --------------------------------------------------------------------------
static void test_touch_cal_solve() {
    TouchCal c;
    // reference board: channel 0x90 follows X, 0xD0 follows Y
    const long a[4] = { 526, 3443, 526, 3443 }, b[4] = { 750, 750, 3377, 3377 };
    TEST_ASSERT_TRUE(touchCalSolve(a, b, c));
    TEST_ASSERT_EQUAL_INT(526, c.xmin);  TEST_ASSERT_EQUAL_INT(3443, c.xmax);
    TEST_ASSERT_EQUAL_INT(750, c.ymin);  TEST_ASSERT_EQUAL_INT(3377, c.ymax);
    TEST_ASSERT_EQUAL_UINT8(1, c.axis);
    // panel with the channels swapped: 0xD0 follows X -> axis 0
    TEST_ASSERT_TRUE(touchCalSolve(b, a, c));
    TEST_ASSERT_EQUAL_UINT8(0, c.axis);
    TEST_ASSERT_EQUAL_INT(526, c.xmin);  TEST_ASSERT_EQUAL_INT(3377, c.ymax);
    // mirrored X: the range comes out reversed, and still maps the crosses to 30 / 290 px
    const long m[4] = { 3443, 526, 3443, 526 };
    TEST_ASSERT_TRUE(touchCalSolve(m, b, c));
    TEST_ASSERT_EQUAL_INT(3443, c.xmin); TEST_ASSERT_EQUAL_INT(526, c.xmax);
    TEST_ASSERT_EQUAL_INT(30, touchCalMap(3443, c.xmin, c.xmax, 320));
    TEST_ASSERT_EQUAL_INT(290, touchCalMap(526, c.xmin, c.xmax, 320));
    // taps all in one spot: rejected
    const long s[4] = { 2000, 2100, 2050, 2080 };
    TEST_ASSERT_FALSE(touchCalSolve(s, s, c));
}

static void test_gear_estimate() {
    static const float ratios[] = GEAR_RATIOS;
    for (int g = 0; g < GEAR_COUNT; g++) {
        float kmh = 60;
        float wheelRpm = (kmh / 3.6f) / TIRE_CIRCUMFERENCE_M * 60.0f;
        TEST_ASSERT_EQUAL_INT(g + 1, estimateGear(wheelRpm * ratios[g] * FINAL_DRIVE, kmh));
    }
#if GEAR_COUNT > 0
    TEST_ASSERT_EQUAL_INT(0, estimateGear(900, 1));          // standing still
    TEST_ASSERT_EQUAL_INT(0, estimateGear(6000, 20));        // clutch slipping, no ratio fits
#else
    TEST_ASSERT_EQUAL_INT(-1, estimateGear(3000, 60));       // gears disabled: always hidden
#endif
}

static void test_model_levels_and_shift() {
    GaugeModel m;
    GaugeView v;
    bus::clear();
    g_host_ms = 10000;
    m.reset(g_host_ms);
    m.setShiftRpm(6500);
    settle(m, v, 6600, 100, COOLANT_CRIT + 1, VOLT_LOW_CRIT - 0.5f, IAT_WARN + 1);
    TEST_ASSERT_TRUE(v.rpmValid);
    TEST_ASSERT_TRUE(v.shift);
    TEST_ASSERT_EQUAL(LV_CRIT, v.coolLvl);
    TEST_ASSERT_EQUAL(LV_CRIT, v.voltLvl);
    TEST_ASSERT_EQUAL(LV_WARN, v.iatLvl);
    TEST_ASSERT_EQUAL_INT(0, v.rpmText % RPM_DISPLAY_STEP);
    settle(m, v, 3000, 60, COOLANT_COLD - 5, 14.0f, 30);
    TEST_ASSERT_FALSE(v.shift);
    TEST_ASSERT_EQUAL(LV_COLD, v.coolLvl);
    TEST_ASSERT_EQUAL(LV_NORMAL, v.voltLvl);
    TEST_ASSERT_EQUAL_INT(6600, v.sessionPeak);
    m.resetPeaks();
    settle(m, v, 3000, 60, 90, 14, 30, 2);
    TEST_ASSERT_EQUAL_INT(3000, v.sessionPeak);
}

static void test_model_marks_stale_values() {
    GaugeModel m;
    GaugeView v;
    bus::clear();
    g_host_ms = 50000;
    m.reset(g_host_ms);
    settle(m, v, 2000, 50, 90, 14, 30, 5);
    TEST_ASSERT_TRUE(v.rpmValid && v.speedValid && v.coolValid);
    g_host_ms += DATA_STALE_MS + 10;                         // nothing published since
    GaugeSnapshot s;
    bus::snapshot(s);
    m.update(s, g_host_ms, "TEST", v);
    TEST_ASSERT_FALSE(v.rpmValid);
    TEST_ASSERT_FALSE(v.speedValid);
    TEST_ASSERT_FALSE(v.coolValid);
}

// ---- simulator ----------------------------------------------------------------------------------
static void test_simulator_stays_physical() {
    SimSource sim(false);
    bus::clear();
    g_host_ms = 1000;
    sim.begin();
    bool sawShift = false;
    float maxSpd = 0;
    for (int i = 0; i < 200 * 100; i++) {                    // 200 s at 100 Hz
        g_host_ms += 10;
        sim.step(0.01f);
        if (i % 10) continue;
        GaugeSnapshot s;
        bus::snapshot(s);
        if (!s.stamp[CH_RPM]) continue;                      // first publish is 40 ms in
        for (int c = 0; c < CH_COUNT; c++) TEST_ASSERT_FALSE(isnan(s.value[c]));
        TEST_ASSERT_TRUE(s.value[CH_RPM] >= 0 && s.value[CH_RPM] <= RPM_MAX);
        TEST_ASSERT_TRUE(s.value[CH_SPEED] >= 0 && s.value[CH_SPEED] < 260);
#if GEAR_COUNT > 0
        TEST_ASSERT_TRUE(s.value[CH_GEAR] >= 0 && s.value[CH_GEAR] <= GEAR_COUNT);
#else
        TEST_ASSERT_EQUAL_UINT32(0, s.stamp[CH_GEAR]);       // GEAR_COUNT 0: sim sends no gear
#endif
        TEST_ASSERT_TRUE(s.value[CH_COOLANT] > 20 && s.value[CH_COOLANT] < 125);
        TEST_ASSERT_TRUE(s.value[CH_VOLTAGE] > 9 && s.value[CH_VOLTAGE] < 15.5f);
        if (s.value[CH_RPM] >= RPM_SHIFT) sawShift = true;
        if (s.value[CH_SPEED] > maxSpd) maxSpd = s.value[CH_SPEED];
    }
    TEST_ASSERT_TRUE_MESSAGE(sawShift, "scripted pull never reached the shift light");
    TEST_ASSERT_TRUE(maxSpd > 150);
}

// ---- renderer --------------------------------------------------------------------------------------
static void test_renderer_only_pushes_changes() {
    gauge_ui::begin(push, kThemes[0]);
    GaugeModel m;
    GaugeView v;
    bus::clear();
    g_host_ms = 90000;
    m.reset(g_host_ms);
    settle(m, v, 3000, 80, 90, 14, 30);
    gauge_ui::invalidate();
    gauge_ui::render(v);
    TEST_ASSERT_EQUAL_INT(9, gauge_ui::lastPushedRegions());   // 8 live regions + SETUP button
    gauge_ui::render(v);
    TEST_ASSERT_EQUAL_INT(0, gauge_ui::lastPushedRegions());   // nothing changed
    v.speed = 81;
    gauge_ui::render(v);
    TEST_ASSERT_EQUAL_INT(1, gauge_ui::lastPushedRegions());   // only the speed row
}

static void test_theme_switch_repaints_whole_screen() {
    gauge_ui::begin(push, kThemes[0]);
    for (int t = 0; t < THEME_COUNT; t++) {
        pushedPx = 0;
        memset(fb, 0, sizeof fb);
        gauge_ui::setTheme(kThemes[t]);
        TEST_ASSERT_TRUE(pushedPx >= 320 * 240);
        TEST_ASSERT_EQUAL_HEX16(kThemes[t].background[120 * 320 + 5], fb[120 * 320 + 5]);
        TEST_ASSERT_EQUAL_PTR(&kThemes[t], &gauge_ui::theme());
    }
}

// ---- splash ---------------------------------------------------------------------------------
static void test_credit_integrity() {
    TEST_ASSERT_FALSE(splash_ui::authentic());                     // splash not finished yet
    TEST_ASSERT_EQUAL_HEX32(0x6EEE0E43, splash_ui::creditCrc());   // logo/credit untouched
    gauge_ui::begin(push, kThemes[0]);
    splash_ui::draw(push, kThemes[0]);
    splash_ui::progress(push, kThemes[0], 0.99f);
    TEST_ASSERT_FALSE(splash_ui::authentic());
    splash_ui::progress(push, kThemes[0], 1.0f);
    TEST_ASSERT_TRUE(splash_ui::authentic());
}

static void test_splash_renders_every_theme() {
    TEST_ASSERT_TRUE(fontHas(font_title, "REDLINE"));
    TEST_ASSERT_TRUE(fontHas(font_small, "RACING DASH FOR THE CYD crafted by v0123456789."));
    TEST_ASSERT_TRUE(LOGO_BIRDLAB_W > 60 && LOGO_BIRDLAB_W < 200 && LOGO_BIRDLAB_H < 40);
    for (int t = 0; t < THEME_COUNT; t++) {
        pushedPx = 0;
        memset(fb, 0, sizeof fb);
        splash_ui::draw(push, kThemes[t]);
        TEST_ASSERT_EQUAL_INT(320 * 240, (int)pushedPx);            // exactly one full screen
        long before = pushedPx;
        splash_ui::progress(push, kThemes[t], 0.5f);
        TEST_ASSERT_TRUE(pushedPx - before < 320 * 30);             // progress = small region only
        splash_ui::progress(push, kThemes[t], -3.0f);               // out-of-range input is clamped
        splash_ui::progress(push, kThemes[t], 9.0f);
        // the credit logo actually landed on screen: bright pixels near the bottom
        int bright = 0;
        for (int y = 200; y < 230; y++)
            for (int x = 0; x < 320; x++) if ((fb[y * 320 + x] >> 11) > 26) bright++;
        TEST_ASSERT_TRUE_MESSAGE(bright > 150, kThemes[t].name);
    }
}

// Partial pushes (only the changed bar columns / digit cells) must leave the screen
// pixel-identical to repainting every region from scratch, frame after frame.
#include <vector>
static void test_partial_redraw_matches_full_redraw() {
    gauge_ui::begin(push, kThemes[1]);
    splash_ui::progress(push, kThemes[1], 1.0f);             // official title, as on the device
    GaugeModel m;
    GaugeView base;
    bus::clear();
    g_host_ms = 300000;
    m.reset(g_host_ms);
    settle(m, base, 900, 0, 88, 14, 35);
    base.gear = 1;

    std::vector<GaugeView> frames;
    for (int i = 0; i < 160; i++) {
        GaugeView v = base;
        float ph = i / 160.0f, tri = ph < 0.5f ? ph * 2 : (1 - ph) * 2;
        v.rpmBar = tri * RPM_MAX;
        v.rpmText = (int)(v.rpmBar / RPM_DISPLAY_STEP) * RPM_DISPLAY_STEP;
        v.peakMarker = (i > 85 && i < 130) ? RPM_MAX * 0.9f - (i - 85) * 30 : 0;   // falling peak marker
        v.sessionPeak = i > 40 ? 7200 : 0;
        v.shift = v.rpmBar >= RPM_SHIFT;
        v.shiftFlash = (i / 3) & 1;
        v.speed = (int)(tri * 199);
        v.gear = 1 + i / 40;                                    // gear changes mid-sweep
        if (i == 150) v.rpmValid = v.speedValid = false;        // "--" path
        frames.push_back(v);
    }
    // pass 1: normal incremental rendering, snapshot the screen after every frame
    std::vector<std::vector<uint16_t>> shots;
    long incrementalPx = 0;
    gauge_ui::redrawAll();                                  // clean screen (the splash left its bar in fb)
    for (auto &v : frames) {
        pushedPx = 0;
        gauge_ui::render(v);
        incrementalPx += pushedPx;
        shots.emplace_back(fb, fb + 320 * 240);
    }
    // pass 2: every frame repainted from scratch; must match pass 1 exactly
    for (size_t i = 0; i < frames.size(); i++) {
        gauge_ui::redrawAll();
        gauge_ui::render(frames[i]);
        int diff = 0;
        for (int p = 0; p < 320 * 240; p++) diff += fb[p] != shots[i][p];
        char msg[48];
        snprintf(msg, sizeof msg, "frame %d differs in %d px", (int)i, diff);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, diff, msg);
    }
    // and the point of it all: far fewer pixels than repainting the moving regions
    printf("partial redraw: %.0f px/frame on a full sweep\n", (double)incrementalPx / frames.size());
    TEST_ASSERT_TRUE(incrementalPx / (long)frames.size() < 9000);
}

// ---- settings page -----------------------------------------------------------------------------------
static void test_settings_taps() {
    gauge_ui::begin(push, kThemes[0]);
    Settings s;
    TEST_ASSERT_EQUAL(settings_ui::ACT_CHANGED, settings_ui::tap(260, 80, s));   // 3rd theme card
    TEST_ASSERT_EQUAL_UINT8(2, s.theme);
    TEST_ASSERT_EQUAL(settings_ui::ACT_NONE, settings_ui::tap(260, 80, s));      // same again: no-op
    TEST_ASSERT_EQUAL(settings_ui::ACT_CHANGED, settings_ui::tap(203, 163, s));  // CUSTOM source
    TEST_ASSERT_EQUAL_UINT8(SRC_CUSTOM, s.source);
    TEST_ASSERT_EQUAL(settings_ui::ACT_CHANGED, settings_ui::tap(300, 163, s));  // H TEST source
    TEST_ASSERT_EQUAL_UINT8(SRC_HONDA_TEST, s.source);
    TEST_ASSERT_EQUAL(settings_ui::ACT_CLOSE, settings_ui::tap(280, 14, s));     // DONE
    TEST_ASSERT_EQUAL(settings_ui::ACT_RESET_PEAK, settings_ui::tap(117, 229, s));
    TEST_ASSERT_EQUAL(settings_ui::ACT_CHANGED, settings_ui::tap(191, 229, s));  // PANELS cycles
    TEST_ASSERT_EQUAL_UINT8(PANELS_STANDARD, s.panels);
    settings_ui::tap(191, 229, s);
    TEST_ASSERT_EQUAL_UINT8(PANELS_HYBRID, s.panels);
    settings_ui::tap(191, 229, s);
    TEST_ASSERT_EQUAL_UINT8(PANELS_AUTO, s.panels);
    TEST_ASSERT_EQUAL(settings_ui::ACT_CHANGED, settings_ui::tap(266, 229, s));  // GEAR
    TEST_ASSERT_EQUAL_UINT8(GEARMODE_OFF, s.gearMode);
    settings_ui::tap(266, 229, s);
    TEST_ASSERT_EQUAL_UINT8(GEARMODE_AUTO, s.gearMode);
    TEST_ASSERT_EQUAL(settings_ui::ACT_NONE, settings_ui::tap(160, 144, s));     // between theme names and sources
}

static void test_settings_limits() {
    gauge_ui::begin(push, kThemes[0]);
    Settings s;
    for (int i = 0; i < 40; i++) settings_ui::tap(129, 205, s);                  // SHIFT +
    TEST_ASSERT_EQUAL_UINT16(Settings::kShiftMax, s.shiftRpm);
    for (int i = 0; i < 40; i++) settings_ui::tap(21, 205, s);                   // SHIFT -
    TEST_ASSERT_EQUAL_UINT16(Settings::kShiftMin, s.shiftRpm);
#if BACKLIGHT_DIMMING
    for (int i = 0; i < 10; i++) settings_ui::tap(181, 205, s);                  // BRIGHTNESS -
    TEST_ASSERT_EQUAL_UINT8(Settings::kBrightMin, s.brightness);
    for (int i = 0; i < 10; i++) settings_ui::tap(289, 205, s);                  // BRIGHTNESS +
    TEST_ASSERT_EQUAL_UINT8(100, s.brightness);
#else
    s.brightness = 100;                                                          // row hidden: taps ignored
    TEST_ASSERT_EQUAL(settings_ui::ACT_NONE, settings_ui::tap(181, 205, s));
    TEST_ASSERT_EQUAL(settings_ui::ACT_NONE, settings_ui::tap(289, 205, s));
    TEST_ASSERT_EQUAL_UINT8(100, s.brightness);
#endif
    bool beep = s.beep;
    settings_ui::tap(40, 229, s);                                                // BEEP toggle
    TEST_ASSERT_NOT_EQUAL(beep, s.beep);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_credit_integrity);             // first: needs a fresh boot state
    RUN_TEST(test_rgb565_and_blend);
    RUN_TEST(test_canvas_clips_and_rejects_oversize);
    RUN_TEST(test_tabular_digits_have_equal_width);
    RUN_TEST(test_fonts_cover_every_ui_string);
    RUN_TEST(test_font_tables_sorted);
    RUN_TEST(test_theme_backgrounds_match_slot_geometry);
    RUN_TEST(test_serial_key_value);
    RUN_TEST(test_serial_json_and_aliases);
    RUN_TEST(test_serial_ignores_garbage);
    RUN_TEST(test_obd_parse);
    RUN_TEST(test_obd_adapter_selection);
    RUN_TEST(test_obd_supported_pid_mask);
    RUN_TEST(test_obd_hybrid_pids);
    RUN_TEST(test_honda_kline_frames);
    RUN_TEST(test_touch_cal_solve);
    RUN_TEST(test_drag_timer_constant_accel);
    RUN_TEST(test_drag_timer_lift_and_arming);
    RUN_TEST(test_drag_timer_throttle_start);
    RUN_TEST(test_run_log);
    RUN_TEST(test_timer_ui_renders_every_state);
    RUN_TEST(test_drag_timer_owner_flow);
    RUN_TEST(test_stamp_ahead_of_now_is_fresh);
    RUN_TEST(test_sim_drag_launch_from_line);
    RUN_TEST(test_timer_ui_partial_pushes);
    RUN_TEST(test_model_hybrid_panels_and_gear_off);
    RUN_TEST(test_gear_estimate);
    RUN_TEST(test_model_levels_and_shift);
    RUN_TEST(test_model_marks_stale_values);
    RUN_TEST(test_simulator_stays_physical);
    RUN_TEST(test_renderer_only_pushes_changes);
    RUN_TEST(test_partial_redraw_matches_full_redraw);
    RUN_TEST(test_theme_switch_repaints_whole_screen);
    RUN_TEST(test_splash_renders_every_theme);
    RUN_TEST(test_settings_taps);
    RUN_TEST(test_settings_limits);
    return UNITY_END();
}
