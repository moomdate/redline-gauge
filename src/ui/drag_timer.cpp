#include "ui/drag_timer.h"
#include "config.h"
#include <math.h>
#include <string.h>

constexpr float DragTimer::kThKmh[];
constexpr float DragTimer::kThMph[];

// segment -> (begin threshold, end threshold); -1 = the launch
static const int8_t kSegFrom[SEG_COUNT] = { -1, 0, 1, -1, 2 };
static const int8_t kSegTo[SEG_COUNT]   = {  0, 1, 2,  3, 3 };

static const float kStillKmh   = 1.0f;     // below this the car counts as stopped
static const float kArmMs      = 0;        // stopped -> ready at once
static const float kRearmMs    = 0;        // after a result: stopped -> ready for the next run
static const float kThrStart   = 10;       // throttle % that starts the clock (when known)
static const float kLiftKmh    = 10;       // speed this far under the run's max ends it
static const float kMinLogKmh  = 30;       // a run must get past this to be logged
static const float kStallMs    = 3000;     // no new top speed for this long (cruising) ends it

// Signed age: the sample may carry a stamp a few ms AHEAD of `now` (published on the data
// core after the UI read its clock). Unsigned maths made that look 49 days old - the run was
// "discarded" the instant it started and READY was armed twice.
static bool freshSample(uint32_t stamp, uint32_t now) {
    return stamp && (int32_t)(now - stamp) < (int32_t)DATA_STALE_MS;
}

// ---- log -------------------------------------------------------------------------------
void RunLog::add(RunRecord r) {
    r.seq = (uint16_t)((r.seq & RUN_MPH) | (nextSeq++ & 0x7FFF));
    if (count < kMax) count++;
    memmove(&runs[1], &runs[0], sizeof(RunRecord) * (count - 1));
    runs[0] = r;
}

void RunLog::clear() {
    count = 0;
    nextSeq = 1;
    memset(runs, 0, sizeof runs);
}

uint16_t RunLog::best(int seg, bool mph) const {
    uint16_t b = RUN_NONE;
    for (int i = 0; i < count; i++)
        if (((runs[i].seq & RUN_MPH) != 0) == mph && runs[i].cs[seg] < b) b = runs[i].cs[seg];
    return b;
}

// ---- timer -----------------------------------------------------------------------------
void DragTimer::setMph(bool mph) { wantMph_ = mph; if (state_ != TS_RUN) applyUnits(); }
void DragTimer::applyUnits() {
    mph_ = wantMph_;
    for (int i = 0; i < kThresholds; i++) th_[i] = mph_ ? kThMph[i] * 1.609344f : kThKmh[i];
}

void DragTimer::again() {
    state_ = lastStamp_ ? TS_MOVING : TS_NO_SPEED;
    stillSince_ = -1;
    memset(crossed_, 0, sizeof crossed_);
    memset(newBest_, 0, sizeof newBest_);
    maxV_ = 0;
}

void DragTimer::begin(double t) {
    applyUnits();                          // a units change made while idle / mid-run
    state_ = TS_RUN;
    start_ = zeroT_;                       // provisional: the last standstill sample
    firstT_ = t;
    firstV_ = 0;
    maxT_ = t;
    refined_ = false;
    memset(crossed_, 0, sizeof crossed_);
    memset(newBest_, 0, sizeof newBest_);
    maxV_ = 0;
}

double DragTimer::segBegin(int seg) const {
    int i = kSegFrom[seg];
    if (i < 0) return state_ == TS_RUN || hasResult() ? start_ : -1;
    return crossed_[i] ? cross_[i] : -1;
}

double DragTimer::segEnd(int seg) const {
    int i = kSegTo[seg];
    return crossed_[i] ? cross_[i] : -1;
}

bool DragTimer::segDone(int seg) const { return segBegin(seg) >= 0 && segEnd(seg) >= 0; }

float DragTimer::segAge(int seg, uint32_t now) const {
    if (!segDone(seg)) return -1;
    double age = (now - segEnd(seg)) / 1000.0;
    return age < 0 ? 0 : (float)age;
}

bool DragTimer::segActive(int seg) const {
    return state_ == TS_RUN && segBegin(seg) >= 0 && segEnd(seg) < 0;
}

float DragTimer::segTime(int seg, uint32_t now) const {
    double b = segBegin(seg), e = segEnd(seg);
    if (b < 0) return -1;
    if (e >= 0) return (float)((e - b) / 1000.0);
    if (state_ == TS_RUN) return (float)((now - b) / 1000.0);
    return -1;
}

float DragTimer::segProgress(int seg) const {
    if (segDone(seg)) return 1;
    if (!segActive(seg)) return 0;
    float lo = kSegFrom[seg] < 0 ? 0 : th_[kSegFrom[seg]], hi = th_[kSegTo[seg]];
    float p = (lastV_ - lo) / (hi - lo);
    return p < 0 ? 0 : p > 1 ? 1 : p;
}

float DragTimer::elapsed(uint32_t now) const {
    if (state_ == TS_RUN) return (float)((now - start_) / 1000.0);
    if (state_ == TS_FINISH) return (float)((cross_[3] - start_) / 1000.0);
    return 0;
}

TimerEvent DragTimer::end(const RunLog &log) {
    if (maxV_ < kMinLogKmh) { state_ = TS_NO_RESULT; stillSince_ = -1; return TE_DISCARD; }
    rec_ = {};
    for (int s = 0; s < SEG_COUNT; s++) {
        rec_.cs[s] = RUN_NONE;
        if (!segDone(s)) continue;
        double cs = (segEnd(s) - segBegin(s)) / 10.0;
        rec_.cs[s] = cs < 0 ? 0 : cs > 65000 ? 65000 : (uint16_t)lround(cs);
        newBest_[s] = rec_.cs[s] < log.best(s, mph_);
    }
    rec_.seq = mph_ ? RUN_MPH : 0;               // RunLog::add fills in the number
    rec_.maxKmh = (uint16_t)lroundf(maxV_);
    double toMax = (maxT_ - start_) / 10.0;
    rec_.toMaxCs = toMax < 0 ? 0 : toMax > 65000 ? 65000 : (uint16_t)lround(toMax);
    stillSince_ = -1;
    state_ = crossed_[3] ? TS_FINISH : TS_SAVED;
    return state_ == TS_FINISH ? TE_FINISH : TE_SAVED;
}

// Throttle sample (sources that know the pedal: SIM, HONDA K, SERIAL tps=). Pressing it
// while READY starts the clock at that exact moment, which is what "0-100 from when I hit
// the throttle" means. Sources without throttle start on the first movement instead.
TimerEvent DragTimer::throttle(float pct, uint32_t stamp, uint32_t now) {
    if (!freshSample(stamp, now) || stamp == lastThrStamp_) return TE_NONE;
    lastThrStamp_ = stamp;
    bool pressed = pct >= kThrStart, was = lastThr_ >= kThrStart;
    lastThr_ = pct;
    if (state_ == TS_STAGED && pressed && !was) {
        begin(stamp);
        start_ = stamp;
        refined_ = true;                   // exact start, no back-estimate needed
        return TE_START;
    }
    return TE_NONE;
}

TimerEvent DragTimer::update(float v, uint32_t stamp, uint32_t now, const RunLog &log) {
    if (!freshSample(stamp, now)) {
        if (state_ == TS_RUN) return end(log);        // speed vanished mid-run
        if (!hasResult() && state_ != TS_NO_RESULT) state_ = TS_NO_SPEED;
        return TE_NONE;
    }
    if (stamp == lastStamp_) return TE_NONE;          // nothing new this frame
    bool first = lastStamp_ == 0;
    lastStamp_ = stamp;
    double t = stamp;
    if (v < 0) v = 0;
    TimerEvent ev = TE_NONE;

    switch (state_) {
    case TS_NO_SPEED:
    case TS_MOVING:
    case TS_STAGED:
        if (v < kStillKmh) {
            if (stillSince_ < 0) stillSince_ = t;
            zeroT_ = t;
            if (state_ != TS_STAGED && t - stillSince_ >= kArmMs) { state_ = TS_STAGED; ev = TE_ARMED; }
            else if (state_ == TS_NO_SPEED) state_ = TS_MOVING;   // "hold still" until armed
        } else if (state_ == TS_STAGED && !first) {
            begin(t);
            firstV_ = v;
            ev = TE_START;
        } else {
            state_ = TS_MOVING;
            stillSince_ = -1;
        }
        break;

    case TS_RUN: {
        // Second moving sample: back-estimate the launch from the initial acceleration
        // (the car left 0 somewhere between zeroT_ and the first moving sample).
        if (!refined_ && t > firstT_) {
            refined_ = true;
            float a = (v - firstV_) / (float)((t - firstT_) / 1000.0);     // km/h per s
            if (a > 0.5f) {
                double s = firstT_ - firstV_ / a * 1000.0;
                start_ = s < zeroT_ ? zeroT_ : s > firstT_ ? firstT_ : s;
            }
        }
        for (int i = 0; i < kThresholds; i++)
            if (!crossed_[i] && v >= th_[i]) {
                float f = v > lastV_ ? (th_[i] - lastV_) / (v - lastV_) : 1;
                if (f < 0) f = 0;
                cross_[i] = lastT_ + f * (t - lastT_);
                crossed_[i] = true;
                ev = TE_SPLIT;
            }
        if (v > maxV_) { maxV_ = v; maxT_ = t; }
        bool moved = maxV_ >= kStillKmh;
        if (crossed_[3]) ev = end(log);
        else if (!moved && t - start_ >= kStallMs) {      // throttle blip, car never moved
            state_ = TS_STAGED;
            ev = TE_NONE;
        } else if (moved && (v < maxV_ - kLiftKmh || v < kStillKmh || t - maxT_ >= kStallMs)) {
            ev = end(log);
        }
        break;
    }

    case TS_FINISH:
    case TS_SAVED:
    case TS_NO_RESULT:
        if (v < kStillKmh) {
            if (stillSince_ < 0) stillSince_ = t;
            zeroT_ = t;
            if (t - stillSince_ >= kRearmMs) {        // parked again: ready for the next run
                memset(crossed_, 0, sizeof crossed_);
                memset(newBest_, 0, sizeof newBest_);
                maxV_ = 0;
                state_ = TS_STAGED;
                ev = TE_ARMED;
            }
        } else {
            stillSince_ = -1;
        }
        break;
    }
    lastT_ = t;
    lastV_ = v;
    return ev;
}
