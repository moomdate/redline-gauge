#include "ui/gauge_model.h"
#include "config.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// Signed age: a sample the data core published a moment AFTER `now` was read is fresh
// (unsigned maths called it 49 days old and blanked the value for one frame).
static bool fresh(const GaugeSnapshot &s, Channel ch, uint32_t now) {
    return s.stamp[ch] && (int32_t)(now - s.stamp[ch]) < (int32_t)DATA_STALE_MS;
}

// Exponential approach with a time constant, frame-rate independent.
static float approach(float cur, float target, float dtMs, float tauMs) {
    float k = 1.0f - expf(-dtMs / tauMs);
    return cur + (target - cur) * k;
}

int estimateGear(float rpm, float speedKmh) {
#if GEAR_COUNT > 0
    if (speedKmh < 3 || rpm < 400) return 0;
    static const float ratios[GEAR_COUNT] = GEAR_RATIOS;
    float wheelRpm = (speedKmh / 3.6f) / TIRE_CIRCUMFERENCE_M * 60.0f;
    float overall = rpm / wheelRpm / FINAL_DRIVE;
    int best = 0;
    float bestErr = GEAR_TOLERANCE;
    for (int i = 0; i < GEAR_COUNT; i++) {
        float err = fabsf(overall - ratios[i]) / ratios[i];
        if (err < bestErr) { bestErr = err; best = i + 1; }
    }
    return best;
#else
    (void)rpm; (void)speedKmh;
    return -1;
#endif
}

void GaugeModel::reset(uint32_t now) {
    t0_ = now;
    last_ = now;
    rpmSmooth_ = speedSmooth_ = 0;
    hybridSeen_ = false;
    resetPeaks();
}

void GaugeModel::resetPeaks() {
    peak_ = 0;
    peakAt_ = 0;
    sessionPeak_ = 0;
}

void GaugeModel::update(const GaugeSnapshot &s, uint32_t now, const char *modeName, GaugeView &v) {
    float dt = (float)(now - last_);
    if (dt > 200) dt = 200;
    last_ = now;

    // ---- RPM -----------------------------------------------------------------
    v.rpmValid = fresh(s, CH_RPM, now);
    float rpm = v.rpmValid ? s.value[CH_RPM] : 0;
    if (rpm < 0) rpm = 0;
    // OBD delivers RPM at ~5-10 Hz; a 70 ms time constant turns that into smooth
    // motion without visible lag. Falling edge slightly slower, like a real needle.
    rpmSmooth_ = approach(rpmSmooth_, rpm, dt, rpm > rpmSmooth_ ? 60.0f : 90.0f);
    v.rpmBar = rpmSmooth_;
    v.rpmText = (int)(rpmSmooth_ / RPM_DISPLAY_STEP + 0.5f) * RPM_DISPLAY_STEP;
    if (v.rpmText > 99999) v.rpmText = 99999;

    if (rpmSmooth_ >= peak_) { peak_ = rpmSmooth_; peakAt_ = now; }
    else if (now - peakAt_ > PEAK_HOLD_MS) {
        peak_ -= dt * (RPM_MAX / 2500.0f);          // falls full-scale in 2.5 s
        if (peak_ < rpmSmooth_) peak_ = rpmSmooth_;
    }
    v.peakMarker = (v.rpmValid && peak_ > rpmSmooth_ + RPM_MAX * 0.02f) ? peak_ : 0;
    if (v.rpmValid && rpm > sessionPeak_) sessionPeak_ = (int)(rpm / 10 + 0.5f) * 10;
    v.sessionPeak = sessionPeak_;

    v.shift = v.rpmValid && rpmSmooth_ >= shiftRpm_;
    v.shiftFlash = ((now / 70) & 1) == 0;             // ~7 Hz
    v.blink = ((now / 350) & 1) == 0;

    // ---- speed / gear ----------------------------------------------------------
    v.speedValid = fresh(s, CH_SPEED, now);
    float spd = v.speedValid ? s.value[CH_SPEED] : 0;
    speedSmooth_ = approach(speedSmooth_, spd, dt, 120.0f);
    v.speed = (int)(speedSmooth_ + 0.5f);
    if (v.speed > 999) v.speed = 999;
    if (v.speed < 0) v.speed = 0;

    if (gearOff_) v.gear = -1;
    else if (fresh(s, CH_GEAR, now)) v.gear = (int)(s.value[CH_GEAR] + 0.5f);
    else if (v.rpmValid && v.speedValid) v.gear = estimateGear(rpm, spd);
    else v.gear = -1;

    // ---- side panels -------------------------------------------------------------
    v.coolValid = fresh(s, CH_COOLANT, now);
    v.coolant = (int)lroundf(s.value[CH_COOLANT]);
    v.coolLvl = v.coolant >= COOLANT_CRIT ? LV_CRIT
              : v.coolant >= COOLANT_WARN ? LV_WARN
              : v.coolant < COOLANT_COLD  ? LV_COLD : LV_NORMAL;

    v.voltValid = fresh(s, CH_VOLTAGE, now);
    v.volt = s.value[CH_VOLTAGE];
    v.voltLvl = (v.volt < VOLT_LOW_CRIT || v.volt > VOLT_HIGH_CRIT) ? LV_CRIT
              : (v.volt < VOLT_LOW_WARN || v.volt > VOLT_HIGH_WARN) ? LV_WARN : LV_NORMAL;

    v.iatValid = fresh(s, CH_IAT, now);
    v.iat = (int)lroundf(s.value[CH_IAT]);
    v.iatLvl = v.iat >= IAT_CRIT ? LV_CRIT : v.iat >= IAT_WARN ? LV_WARN : LV_NORMAL;

    v.socValid = fresh(s, CH_HV_SOC, now);
    v.soc = (int)lroundf(s.value[CH_HV_SOC]);
    v.socLvl = v.soc <= SOC_LOW_CRIT ? LV_CRIT : v.soc <= SOC_LOW_WARN ? LV_WARN : LV_NORMAL;
    v.kwValid = fresh(s, CH_HV_KW, now);
    v.kw = s.value[CH_HV_KW];
    if (v.socValid || v.kwValid) hybridSeen_ = true;
    v.hybrid = panels_ == PANELS_HYBRID || (panels_ == PANELS_AUTO && hybridSeen_);

    // ---- status bar ---------------------------------------------------------------
    v.mode = modeName;
    v.link = s.link;
    bool slow = ((now / 500) & 1) == 0;
    switch (s.link) {
        case Link::Live:      v.dotColor = 0x48ec63; v.dotOn = true; break;
        case Link::Simulated: v.dotColor = 0x2fa8ff; v.dotOn = true; break;
        case Link::Connecting:v.dotColor = 0xf5df3c; v.dotOn = slow; break;
        case Link::Error:     v.dotColor = 0xff4741; v.dotOn = true; break;
        default:              v.dotColor = 0x6c7a84; v.dotOn = true; break;
    }
    v.testTag[0] = 0;
    v.testLetter = 0;
    int idx;
    char letter;
    if (modeName && !strcmp(modeName, "HONDA TEST") && sscanf(s.linkMsg, "%c d[%d]", &letter, &idx) == 2) {
        snprintf(v.testTag, sizeof v.testTag, "d[%d]", idx);
        v.testLetter = letter;
    }
    if (s.linkMsg[0]) {
        snprintf(v.right, sizeof(v.right), "%s", s.linkMsg);
    } else {
        uint32_t sec = (now - t0_) / 1000;
        if (sec >= 3600) snprintf(v.right, sizeof(v.right), "%u:%02u:%02u",
                                  (unsigned)(sec / 3600), (unsigned)(sec / 60 % 60), (unsigned)(sec % 60));
        else snprintf(v.right, sizeof(v.right), "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
    }
}
