// Host-side preview: compiles the REAL firmware UI + simulator code for macOS/Linux
// and writes frames as PPM, so layout changes can be checked without flashing.
//   tools/host_preview/run.sh           -> tools/host_preview/out/*.png (+ sim.gif)
#include <stdio.h>
#include <string.h>
#include <vector>
#include "data/gauge_bus.h"
#include "data/sim_source.h"
#include "ui/gauge_model.h"
#include "ui/gauge_ui.h"
#include "ui/settings_ui.h"
#include "ui/splash_ui.h"
#include "ui/theme.h"
#include "ui/drag_timer.h"
#include "ui/timer_ui.h"
#include "ui/link_ui.h"
#include "data/link_status.h"
#include "config.h"

extern uint32_t g_host_ms;
static uint16_t fb[320 * 240];
static long pushedPixels = 0;

static void push(int x, int y, int w, int h, const uint16_t *px) {
    for (int r = 0; r < h; r++) memcpy(&fb[(y + r) * 320 + x], px + r * w, w * 2);
    pushedPixels += (long)w * h;
}

static void save(const char *path) {
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6 320 240 255\n");
    for (int i = 0; i < 320 * 240; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3 | (c >> 13)), (uint8_t)(((c >> 5) & 63) << 2 | ((c >> 9) & 3)),
                           (uint8_t)((c & 31) << 3 | ((c >> 2) & 7)) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static float g_soc = -1, g_kw = 0;      // hybrid frames: >= 0 publishes HV data
static float g_afr = -1;                // HONDA K frames: >= 0 publishes AFR
static float g_tps = -1;                // >= 0 publishes throttle
static bool  g_gearOff = false;

static void staticFrame(const char *path, float rpm, float spd, float clt, float v, float iat, int gear,
                        Link link, const char *msg, const char *mode) {
    GaugeModel m;
    GaugeView view;
    m.setShiftRpm(7000);
    m.setGearHidden(g_gearOff);
    bus::clear();
    g_host_ms = 100000;
    m.reset(g_host_ms - 754000);
    bus::setLink(link, msg);
    // settle the smoothing filters
    for (int i = 0; i < 60; i++) {
        g_host_ms += 33;
        if (rpm >= 0) bus::publish(CH_RPM, rpm);
        if (spd >= 0) bus::publish(CH_SPEED, spd);
        if (clt > -99) bus::publish(CH_COOLANT, clt);
        if (v > 0) bus::publish(CH_VOLTAGE, v);
        if (iat > -99) bus::publish(CH_IAT, iat);
        if (gear >= 0) bus::publish(CH_GEAR, gear);
        if (g_soc >= 0) { bus::publish(CH_HV_SOC, g_soc); bus::publish(CH_HV_KW, g_kw); }
        if (g_afr >= 0) bus::publish(CH_AFR, g_afr);
        if (g_tps >= 0) bus::publish(CH_THROTTLE, g_tps);
        GaugeSnapshot s;
        bus::snapshot(s);
        m.update(s, g_host_ms, mode, view);
    }
    view.shiftFlash = true; view.blink = true; view.dotOn = true;
    gauge_ui::invalidate();
    gauge_ui::render(view);
    save(path);
}

// Drag timer: synthetic 0 -> 205 km/h pull, saved at a few moments.
static void timerFrames() {
    gauge_ui::setTheme(kThemes[THEME_ICE]);
    DragTimer t; RunLog log; log.clear();
    RunRecord old = {};                                  // an earlier, slower run in the log
    uint16_t oldCs[SEG_COUNT] = { 698, 181, 452, 2290, 0 };
    for (int s = 0; s < SEG_COUNT; s++) old.cs[s] = oldCs[s];
    old.cs[SEG_160_200] = 1200; old.maxKmh = 201;
    log.add(old);
    uint32_t ms = 10000;
    t.again();
    auto shot = [&](const char *path) { timer_ui::invalidate(); timer_ui::render(t, log, ms, true); save(path); };
    ms += 200; t.update(0, ms, ms, log);                 // first stopped sample: READY at once
    ms += 200;                                           // READY!! blink phase: on (ms/400 even)
    shot("out/timer_ready.ppm");
    float v = 0;
    bool s1 = false, s2 = false, s3 = false;
    while (t.state() != TS_FINISH) {
        ms += 100;
        float a = v < 100 ? 15.5f : v < 160 ? 9.5f : 6.0f;   // km/h per s
        v += a * 0.1f;
        TimerEvent ev = t.update(v, ms, ms, log);
        if (!s1 && v >= 90)  { shot("out/timer_run.ppm"); s1 = true; }
        if (!s2 && ev == TE_SPLIT && v >= 100 && v < 120) { ms += 150; shot("out/timer_stamp.ppm"); s2 = true; }
        if (!s3 && v >= 144) { shot("out/timer_run2.ppm"); s3 = true; }
    }
    ms += 1000;                                          // flash over: boxes settle
    shot("out/timer_finish.ppm");
    log.add(t.record());
    for (int i = 0; i < 40; i++) { ms += 100; t.update(0, ms, ms, log); }   // stop: re-arms
    v = 0;
    while (v < 143) { ms += 100; v += 1.4f; t.update(v, ms, ms, log); }    // lift at ~143
    ms += 100; t.update(128, ms, ms, log);
    ms += 1000;
    shot("out/timer_saved.ppm");
    log.add(t.record());
    timer_ui::drawLog(log);
    save("out/timer_log.ppm");
    timer_ui::tapLog(60, 224);                           // CLEAR LOG -> asks to confirm
    timer_ui::drawLog(log);
    save("out/timer_log_confirm.ppm");
    timer_ui::tapLog(238, 224);                          // CANCEL
}

int main() {
    gauge_ui::begin(push, kThemes[THEME_ICE]);
    for (int t = 0; t < THEME_COUNT; t++) {
        char p[64];
        splash_ui::draw(push, kThemes[t]);
        splash_ui::progress(push, kThemes[t], 1.0f);   // completes the boot credit
        splash_ui::progress(push, kThemes[t], 0.62f);
        snprintf(p, sizeof p, "out/splash_%s.ppm", kThemes[t].key);
        save(p);
    }
    for (int t = 0; t < THEME_COUNT; t++) {
        char p[64];
        gauge_ui::setTheme(kThemes[t]);
        snprintf(p, sizeof p, "out/theme_%s.ppm", kThemes[t].key);
        staticFrame(p, 5600, 86, 87, 13.9f, 42, 3, Link::Live, "", "OBD BT");
        Settings st;
        st.theme = t; st.source = SRC_OBD; st.shiftRpm = 6750; st.brightness = 80;
        settings_ui::draw(st);
        snprintf(p, sizeof p, "out/settings_%s.ppm", kThemes[t].key);
        save(p);
    }
    gauge_ui::setTheme(kThemes[THEME_ICE]);
    staticFrame("out/design.ppm", 5600, 86, 87, 13.9f, 42, 3, Link::Live, "", "OBD BT");
    gauge_ui::setTheme(kThemes[THEME_LIME]);
    staticFrame("out/shift.ppm", 7250, 142, 106, 11.6f, 72, 4, Link::Simulated, "", "SIM AUTO");
    gauge_ui::setUnits(1); staticFrame("out/speed_mph.ppm", 4200, 113, 92, 13.8f, 40, 3, Link::Live, "", "OBD BT"); gauge_ui::setUnits(0);
    staticFrame("out/cold.ppm", 1150, 0, 45, 12.2f, 30, 0, Link::Live, "", "SERIAL");
    gauge_ui::setTheme(kThemes[THEME_AMBER]);
    gauge_ui::setTheme(kThemes[THEME_ICE]);
    g_soc = 61; g_kw = 18.5f; g_gearOff = true;          // Civic e:HEV style: EV drive, gear hidden
    staticFrame("out/hybrid_ev.ppm", 0, 42, 84, 12.4f, 35, -1, Link::Live, "", "OBD BT");
    g_soc = 66; g_kw = -22.4f;                           // braking: regen
    staticFrame("out/hybrid_regen.ppm", 1250, 63, 88, 12.4f, 35, -1, Link::Live, "", "OBD BT");
    g_soc = -1; g_gearOff = false;
    gauge_ui::setTheme(kThemes[THEME_AMBER]);
    // HONDA K / OBD BT: link pill in each state, selectable panels (AFR, THROTTLE), link pages
    {
        auto addRow = [](LinkStatus &k, const char *key, const char *val, bool bad) {
            LinkRow &r = k.rows[k.rowCount++];
            snprintf(r.key, sizeof r.key, "%s", key);
            snprintf(r.value, sizeof r.value, "%s", val);
            r.bad = bad;
        };
        LinkStatus k;
        snprintf(k.title, sizeof k.title, "K-LINE");
        snprintf(k.subtitle, sizeof k.subtitle, "HONDA ECU LINK");
        k.state = LS_LIVE; k.connects = 1; k.rate = 17;
        k.liveSince = 100000 - 83000; k.lastReply = 100000 + 1980;
        gauge_ui::setLink(&k, 100000 + 1980);
        const uint8_t hondaItems[3] = { PI_COOLANT, PI_VOLTAGE, PI_AFR };
        gauge_ui::setPanelItems(hondaItems);
        g_afr = 14.6f;
        staticFrame("out/honda_live.ppm", 1450, 0, 92, 13.6f, -100, -1, Link::Live, "", "HONDA K");
        g_afr = 13.1f;
        gauge_ui::setTheme(kThemes[THEME_LIME]);
        staticFrame("out/honda_rich.ppm", 6200, 0, 98, 13.9f, -100, -1, Link::Live, "", "HONDA K");
        const uint8_t tpsItems[3] = { PI_THROTTLE, PI_INTAKE, PI_AFR };
        gauge_ui::setPanelItems(tpsItems);
        g_afr = 15.6f; g_tps = 37;
        gauge_ui::setTheme(kThemes[THEME_ICE]);
        staticFrame("out/honda_tps.ppm", 3100, 0, 96, 13.8f, 33, -1, Link::Live, "", "HONDA K");
        g_afr = -1; g_tps = -1;
        gauge_ui::setPanelItems(hondaItems);
        k.state = LS_RETRY; snprintf(k.reason, sizeof k.reason, "NO ECU"); k.retryAt = 100000 + 1980 + 1600;
        k.drops = 1; k.rate = 0;
        staticFrame("out/honda_noecu.ppm", -1, -1, -100, -1, -100, -1, Link::Error, "NO ECU", "HONDA K");
        snprintf(k.reason, sizeof k.reason, "K-LINE LOW");
        staticFrame("out/honda_low.ppm", -1, -1, -100, -1, -100, -1, Link::Error, "K-LINE LOW", "HONDA K");
        k.state = LS_CONNECTING; snprintf(k.step, sizeof k.step, "WAKE-UP");
        staticFrame("out/honda_linking.ppm", -1, -1, -100, -1, -100, -1, Link::Connecting, "K-LINE INIT", "HONDA K");

        LinkStatus o;
        snprintf(o.title, sizeof o.title, "OBD BT");
        snprintf(o.subtitle, sizeof o.subtitle, "ELM327 BLUETOOTH");
        o.state = LS_CONNECTING; snprintf(o.step, sizeof o.step, "BT PAIRING");
        const uint8_t stdItems[3] = { PI_COOLANT, PI_VOLTAGE, PI_INTAKE };
        gauge_ui::setPanelItems(stdItems);
        gauge_ui::setLink(&o, 100000 + 1980);
        staticFrame("out/obd_pairing.ppm", -1, -1, -100, -1, -100, -1, Link::Connecting, "BT PAIRING", "OBD BT");
        o.state = LS_RETRY; snprintf(o.reason, sizeof o.reason, "NO ADAPTER"); o.retryAt = 100000 + 1980 + 2500;
        snprintf(o.hint, sizeof o.hint, "Adapter not found: plugged in? powered?");
        staticFrame("out/obd_noadapter.ppm", -1, -1, -100, -1, -100, -1, Link::Error, "NO ADAPTER", "OBD BT");
        o.state = LS_LIVE; o.connects = 1; o.liveSince = 1000;
        staticFrame("out/obd_live.ppm", 2400, 64, 88, 14.1f, 36, 3, Link::Live, "", "OBD BT");
        gauge_ui::setLink(nullptr, 0);

        Settings st;
        st.theme = THEME_ICE;
        LinkStatus live = k;
        live.state = LS_LIVE; live.connects = 3; live.drops = 2; live.rate = 17; live.liveSince = 1000; live.lastReply = 84000;
        live.rowCount = 0;
        addRow(live, "ECHO", "YES", false); addRow(live, "K-LINE IDLE", "OK", false);
        addRow(live, "O2 SENSOR", "0.44 V", false); addRow(live, "AFR (EST.)", "14.7", false);
        link_ui::invalidate();
        link_ui::render(live, 84200, st);
        save("out/link_honda_live.ppm");
        LinkStatus off = live;
        off.state = LS_RETRY; snprintf(off.reason, sizeof off.reason, "NO ECU"); off.retryAt = 86000;
        snprintf(off.hint, sizeof off.hint, "Wiring OK, ECU silent: ignition ON?");
        off.drops = 3; off.lastReply = 80000;
        st.theme = THEME_AMBER;
        link_ui::invalidate();
        link_ui::render(off, 84200, st);
        save("out/link_honda_noecu.ppm");
        LinkStatus ob = o;
        ob.state = LS_RETRY; snprintf(ob.reason, sizeof ob.reason, "BT LOST"); ob.retryAt = 85200;
        snprintf(ob.hint, sizeof ob.hint, "Bluetooth dropped: adapter unplugged?");
        ob.connects = 2; ob.drops = 2; ob.lastReply = 81000; ob.rowCount = 0;
        addRow(ob, "BLUETOOTH", "--", false); addRow(ob, "ADAPTER", "3C:9E:51", false); addRow(ob, "PIDS", "28", false);
        st.theme = THEME_LIME;
        link_ui::invalidate();
        link_ui::render(ob, 84200, st);
        save("out/link_obd_lost.ppm");
        gauge_ui::setPanelItems(stdItems);
        gauge_ui::setTheme(kThemes[THEME_ICE]);
    }
    staticFrame("out/nodata.ppm", -1, -1, -100, -1, -100, -1, Link::Connecting, "BT PAIRING", "OBD BT");

    // ---- run the AUTO simulator for 150 s of virtual time ----
    gauge_ui::setTheme(kThemes[THEME_ICE]);
    SimSource sim(false);
    GaugeModel m;
    GaugeView view;
    bus::clear();
    g_host_ms = 1000;
    sim.begin();
    m.reset(g_host_ms);
    gauge_ui::invalidate();
    long frames = 0;
    pushedPixels = 0;
    int maxRpm = 0, maxSpd = 0, shiftFrames = 0;
    for (int f = 0; f < 150 * 30; f++) {
        for (int k = 0; k < 3; k++) { g_host_ms += 11; sim.step(0.011f); }
        GaugeSnapshot s;
        bus::snapshot(s);
        m.update(s, g_host_ms, sim.name(), view);
        gauge_ui::render(view);
        frames++;
        if (view.rpmText > maxRpm) maxRpm = view.rpmText;
        if (view.speed > maxSpd) maxSpd = view.speed;
        if (view.shift) shiftFrames++;
        if (f % 6 == 0 && f < 150 * 30) {
            char p[64];
            snprintf(p, sizeof p, "out/sim_%05d.ppm", f / 6);
            save(p);
        }
        if (f % 300 == 0)
            printf("t=%5.1fs rpm=%5d spd=%3d gear=%2d clt=%3d v=%.2f iat=%d\n", f / 30.0, view.rpmText,
                   view.speed, view.gear, view.coolant, view.volt, view.iat);
    }
    printf("frames=%ld avg pushed px/frame=%ld (%.1f%% of screen), max rpm %d, max speed %d, shift frames %d\n",
           frames, pushedPixels / frames, 100.0 * pushedPixels / frames / 76800.0, maxRpm, maxSpd, shiftFrames);
    timerFrames();
    return 0;
}
