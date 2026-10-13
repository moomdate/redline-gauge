#include "data/sim_source.h"
#include "data/gauge_bus.h"
#include "config.h"
#include <math.h>
#include <stdlib.h>

volatile float SimSource::touchThrottle = 0;
volatile bool  SimSource::hybrid = false;
volatile bool  SimSource::dragMode = false;
volatile bool  SimSource::restartDrag = false;

// ---- vehicle constants ----------------------------------------------------------
static const float kRatios[] = GEAR_RATIOS;
static const int   kGears = sizeof(kRatios) / sizeof(kRatios[0]);
static const float kMass = 1250.0f * 1.08f;               // kg, incl. rotating inertia
static const float kWheelR = TIRE_CIRCUMFERENCE_M / (2.0f * 3.14159265f);
static const float kSimRpmMax = 8000;                    // the simulated car's rev range (not RPM_MAX)
static const float kLimiter = kSimRpmMax - 300;
static const float kTraction = 6200.0f;                    // N, max drive force at the tyres

static float frand(float a) { return ((rand() & 0xFFFF) / 32767.5f - 1.0f) * a; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float toward(float cur, float target, float dt, float tau) {
    return cur + (target - cur) * (1.0f - expf(-dt / tau));
}

// ---- scripted drive for AUTO mode -------------------------------------------------
enum PhaseKind : uint8_t { P_IDLE, P_ACCEL, P_CRUISE, P_BRAKE };
struct Phase { PhaseKind kind; float targetKmh, throttle, shiftAt, maxTime; };
static const Phase kScript[] = {
    { P_IDLE,     0, 0.00f,              0,  5 },   // warm idle after start-up
    { P_ACCEL,   50, 0.35f,           2700, 15 },   // city
    { P_CRUISE,  50, 0.00f,           2700,  5 },
    { P_ACCEL,   80, 0.55f,           3400, 12 },
    { P_CRUISE,  80, 0.00f,           3400,  5 },
    { P_ACCEL,  175, 1.00f, RPM_SHIFT + 150, 16 },  // full pull through the shift light
    { P_BRAKE,   60, 0.60f,              0, 10 },
    { P_CRUISE,  60, 0.00f,           3000,  4 },
    { P_BRAKE,    0, 0.45f,              0, 12 },
    { P_IDLE,     0, 0.00f,              0,  3 },
    { P_ACCEL,  110, 0.80f,           5200, 12 },   // brisk on-ramp
    { P_CRUISE, 110, 0.00f,           3000,  6 },
    { P_BRAKE,    0, 0.55f,              0, 14 },
};
static const int kPhases = sizeof(kScript) / sizeof(kScript[0]);

// Drag timer screen open: stage at a stop, launch flat out shifting at the limiter, run
// past 200 km/h (the timer's last split), lift, brake to a stop, repeat.
static const Phase kDrag[] = {
    { P_IDLE,     0, 0.00f,         0,  4 },   // READY at the line (the car is put at a stop
                                               // when the timer opens)
    { P_ACCEL,  205, 1.00f, kSimRpmMax - 350, 40 },
    { P_BRAKE,    0, 0.70f,         0, 25 },   // back to the line, then the next run
};
static const int kDragPhases = sizeof(kDrag) / sizeof(kDrag[0]);

void SimSource::script(float dt, float &thr, float &brake, float &shiftAt) {
    if (dragMode != inDrag_ || (dragMode && restartDrag)) {   // switch scripts / timer re-opened
        restartDrag = false;
        inDrag_ = dragMode;
        phase_ = inDrag_ ? 0 : 1;
        phaseT_ = 0;
        if (inDrag_) {                      // timer opened: start from a standstill at once
            speed_ = 0; gear_ = 0; shiftTimer_ = 0; launching_ = false;
            throttle_ = 0; rpm_ = RPM_IDLE;
        }
    }
    const Phase *list = inDrag_ ? kDrag : kScript;
    int count = inDrag_ ? kDragPhases : kPhases;
    if (phase_ >= count) phase_ = 0;
    const Phase &p = list[phase_];
    float kmh = speed_ * 3.6f;
    bool done = false;
    phaseT_ += dt;
    thr = 0; brake = 0;
    if (p.shiftAt > 0) shiftAt = p.shiftAt;
    switch (p.kind) {
        case P_IDLE:   done = phaseT_ > p.maxTime; break;
        case P_ACCEL:  thr = p.throttle; done = kmh >= p.targetKmh; break;
        case P_CRUISE: thr = clampf(0.16f + (p.targetKmh - kmh) * 0.06f, 0, 0.6f);
                       done = phaseT_ > p.maxTime; break;
        case P_BRAKE:  brake = p.throttle; done = kmh <= p.targetKmh + 0.5f; break;
    }
    if (done || phaseT_ > p.maxTime + 20) {
        phase_ = (phase_ + 1) % count;
        if (phase_ == 0 && !inDrag_) phase_ = 1;   // the start-up idle only runs once
        grip_ = 1.0f + frand(0.04f);               // every launch a little different
        power_ = 1.0f + frand(0.025f);             // (heat soak, air temperature …)
        phaseT_ = 0;
    }
}

// ---- engine ----------------------------------------------------------------------------
float SimSource::engineTorque(float rpm) const {
    // A 2.0 turbo four (~210 hp): boost builds to a 300 Nm plateau from 2500 to 5000 rpm,
    // then tails off to ~230 Nm at the limiter. Traction-limited launch: 0-100 in ~6.5 s.
    if (rpm < 2500) return 150.0f + (rpm - 1000.0f) * 0.10f;
    if (rpm < 5000) return 300.0f;
    return 300.0f - (rpm - 5000.0f) * 0.04f;
}

void SimSource::begin() {
    speed_ = 0; gear_ = 0; throttle_ = 0; shiftTimer_ = 0; launching_ = false;
    rpm_ = 0; phase_ = 0; phaseT_ = -1.6f;                  // negative = cranking
    inDrag_ = false;
    coolant_ = 38; iat_ = ambient_ + 2; volt_ = 12.5f;
    acc_ = 0;
    lastMs_ = bus::nowMs();
    bus::setLink(Link::Simulated, "");
}

void SimSource::poll() {
    uint32_t now = bus::nowMs();
    float dt = (now - lastMs_) / 1000.0f;
    if (dt < 0.01f) return;                                  // 100 Hz physics max
    lastMs_ = now;
    step(dt > 0.1f ? 0.1f : dt);
}

void SimSource::step(float dt) {
    float thrIn = 0, brake = 0, shiftAt = RPM_SHIFT + 150;

    // ---- crank & start (AUTO and TOUCH both do this once) ----
    if (phaseT_ < 0) {
        phaseT_ += dt;
        if (phaseT_ < -0.4f) { rpm_ = 230 + frand(40); volt_ = 10.6f + frand(0.2f); }
        else                 { rpm_ = toward(rpm_, 1700, dt, 0.08f); volt_ = toward(volt_, 14.3f, dt, 0.3f); }
        if (phaseT_ >= 0) phaseT_ = 0;
    } else {
        if (touch_) {
            thrIn = clampf(touchThrottle, 0, 1);
            brake = thrIn > 0.05f ? 0 : 0.12f;               // lift off = gentle engine/brake decel
            shiftAt = thrIn > 0.9f ? RPM_SHIFT + 150 : 3200;
        } else {
            script(dt, thrIn, brake, shiftAt);
        }
    }
    bool running = phaseT_ >= 0;
    throttle_ = toward(throttle_, thrIn, dt, 0.12f);         // pedal isn't instantaneous

    float idle = coolant_ < 55 ? 1150 : RPM_IDLE;
    float kmh = speed_ * 3.6f;

    // ---- gearbox logic ----
    if (running) {
        if (shiftTimer_ > 0) {
            shiftTimer_ -= dt;
            if (shiftTimer_ <= 0) gear_ = pendingGear_;
        } else if (gear_ == 0) {
            if (throttle_ > 0.05f) {
                // pick up the highest gear that keeps the engine above ~1600 rpm
                float wheel = speed_ / kWheelR * 60.0f / (2.0f * 3.14159265f);
                gear_ = 1;
                while (gear_ < kGears && wheel * kRatios[gear_] * FINAL_DRIVE > 1600) gear_++;
                launching_ = gear_ == 1;
            }
        } else {
            // kick-down: flooring it drops to the lowest gear that still has rev headroom
            int kick = gear_;
            if (thrIn > 0.85f && !launching_) {
                float wheel = speed_ / kWheelR * 60.0f / (2.0f * 3.14159265f);
                while (kick > 1 && wheel * kRatios[kick - 2] * FINAL_DRIVE < shiftAt - 1800) kick--;
            }
            if (kick < gear_) {
                pendingGear_ = kick; shiftTimer_ = 0.3f;
            } else if (rpm_ > shiftAt && gear_ < kGears && !launching_) {
                pendingGear_ = gear_ + 1; shiftTimer_ = throttle_ > 0.8f ? 0.18f : 0.35f;
            } else if ((kmh < 6 || (brake > 0 && rpm_ < 1300)) && throttle_ < 0.05f) {
                // stopping, or braking below useful revs: clutch in and let it idle
                gear_ = 0; launching_ = false;
            } else if (gear_ > 1 && rpm_ < (throttle_ > 0.3f ? 1500 : 1300) && !launching_) {
                pendingGear_ = gear_ - 1; shiftTimer_ = 0.3f;
            }
        }
    }

    // ---- forces ----
    float wheelRpm = speed_ / kWheelR * 60.0f / (2.0f * 3.14159265f);
    float drive = 0;
    bool clutchIn = shiftTimer_ > 0 || gear_ == 0 || !running;

    if (!clutchIn) {
        float ratio = kRatios[gear_ - 1] * FINAL_DRIVE;
        float matched = wheelRpm * ratio;
        float engineRpm = matched;
        if (launching_) {
            // clutch slipping: engine held at a launch rpm until wheels catch up
            float launchRpm = idle + throttle_ * 2600;
            engineRpm = fmaxf(matched, launchRpm);
            if (matched >= launchRpm * 0.97f) launching_ = false;
        }
        float tq = rpm_ >= kLimiter ? 0 : engineTorque(engineRpm) * throttle_ * power_;
        tq -= (1.0f - throttle_) * (12.0f + engineRpm * 0.006f);   // engine braking
        drive = tq * ratio * 0.9f / kWheelR;
        // front tyres can only push so hard: ~0.5 g, so 1st gear spins rather than rockets
        if (drive > kTraction * grip_) drive = kTraction * grip_;
        rpm_ = toward(rpm_, fmaxf(engineRpm, launching_ ? engineRpm : idle * 0.9f), dt, 0.05f);
        if (rpm_ >= kLimiter) rpm_ = kLimiter - 40 - frand(30);      // bounce off the limiter
    } else {
        // free-revving (neutral or mid-shift): rises with throttle, decays toward idle/target
        float target = running ? idle + throttle_ * (kSimRpmMax - 1500 - idle) : rpm_;
        if (shiftTimer_ > 0 && pendingGear_ > 0)                    // mid-shift: fall toward new gear
            target = fmaxf(wheelRpm * kRatios[pendingGear_ - 1] * FINAL_DRIVE, idle);
        if (running) rpm_ = toward(rpm_, target, dt, shiftTimer_ > 0 ? 0.07f : 0.3f);
        if (running && shiftTimer_ <= 0 && gear_ == 0) rpm_ += frand(8);   // idle hunt
    }

    float drag = 0.5f * 1.2f * 0.68f * speed_ * speed_ + (speed_ > 0.05f ? 0.013f * 1250 * 9.81f : 0);
    float brakeF = brake * 9500.0f * (speed_ > 0.05f ? 1 : 0);
    speed_ += (drive - drag - brakeF) / kMass * dt;
    // Dead stop: below walking pace with nothing pushing, the car is parked at exactly 0
    // (otherwise it crept at 0.2-1 km/h for seconds and the timer saw a false launch).
    if (speed_ < 0 || (speed_ < 0.15f && drive <= 0)) speed_ = 0;

    // ---- thermal & electrical ----
    float load = throttle_ * rpm_ / kSimRpmMax;
    if (running) {
        if (coolant_ < 88) coolant_ += (0.45f + rpm_ / 3500.0f + load * 2.0f) * dt;
        else {
            float target = 88 + load * 22 - fminf(kmh / 40.0f, 1.0f) * 3 + (kmh < 5 ? 3 : 0);
            coolant_ = toward(coolant_, target, dt, 18.0f);
        }
        float iatTarget = ambient_ + 5 + (kmh < 10 ? 12 : 0) - fminf(kmh, 100.0f) / 100 * 3 + load * 8;
        iat_ = toward(iat_, iatTarget, dt, 14.0f);
        float vTarget = rpm_ < 1000 ? 13.85f : 14.25f;
        volt_ = toward(volt_, vTarget + frand(0.04f), dt, 0.8f);
    }

    // hybrid battery: motor assist under throttle, regen while braking/coasting
    {
        float v = speed_;
        float target = throttle_ * (12.0f + v * 1.4f) - brake * v * 2.2f - (throttle_ < 0.05f ? v * 0.15f : 0);
        kw_ = toward(kw_, clampf(target, -45.0f, 75.0f), dt, 0.25f);
        soc_ = clampf(soc_ - kw_ * dt / 40.0f, 35.0f, 78.0f);       // ~1 kWh usable, sped up 4x
    }

    // ---- publish at 25 Hz (roughly what a fast OBD link would give) ----
    acc_ += dt;
    if (acc_ >= 0.04f) {
        acc_ = 0;
        bus::publish(CH_RPM, rpm_);
        bus::publish(CH_SPEED, speed_ * 3.6f);
#if GEAR_COUNT > 0
        bus::publish(CH_GEAR, (float)gear_);          // GEAR_COUNT 0 = no gear shown anywhere
#endif
        bus::publish(CH_COOLANT, coolant_);
        bus::publish(CH_VOLTAGE, volt_);
        bus::publish(CH_IAT, iat_);
        bus::publish(CH_THROTTLE, throttle_ * 100);
        if (hybrid) {
            bus::publish(CH_HV_SOC, soc_);
            bus::publish(CH_HV_KW, kw_);
        }
    }
}
