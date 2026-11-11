#pragma once
// Speed-triggered performance timer: 0-100, 100-120, 120-160 and 0-200 km/h from one run
// (160-200 is kept for the log too). No drawing and no hardware here: it is fed the raw
// speed samples (value + the time they were published) and runs identically on the host.
//
//   open   -> car stopped (or stops)       -> STAGED ("READY!!")  at once, no hold time
//   STAGED -> speed leaves 0               -> RUN  (launch back-estimated from the first
//                                             two moving samples; a throttle sample can
//                                             start it earlier, see throttle())
//   RUN    -> 100 / 120 / 160 / 200 km/h   -> TE_SPLIT: the segment ending there is stamped
//   RUN    -> 200 km/h                     -> FINISH
//   RUN    -> speed drops 10 below its max, or no new top speed for 3 s (cruising)
//                                          -> SAVED with whatever it reached (top speed +
//                                             time to it, and every split passed); runs that
//                                             never got past 30 km/h are not logged
//   result -> stopped, or again()          -> STAGED for the next run
// Each threshold crossing is interpolated between the two samples around it, so the
// times are better than the sample rate (OBD speed is integer km/h at ~5-10 Hz).
#include <stdint.h>

enum Seg : uint8_t { SEG_0_100, SEG_100_120, SEG_120_160, SEG_0_200, SEG_160_200, SEG_COUNT };
static const uint16_t RUN_NONE = 0xFFFF;
// RunRecord::seq top bit: the run was timed in mph (0-60 / 60-80 / 80-100 / 0-120 mph) rather than
// km/h (0-100 / 100-120 / 120-160 / 0-200). Kept in seq so the saved log layout is unchanged.
static const uint16_t RUN_MPH = 0x8000;

struct RunRecord {
    uint16_t seq;                 // run number, 1.. (| RUN_MPH for an mph run)
    uint16_t cs[SEG_COUNT];       // segment times in 1/100 s, RUN_NONE = not reached
    uint16_t maxKmh;
    uint16_t toMaxCs;             // time from launch to the top speed, 1/100 s
};

// Newest-first ring of saved runs. Plain data: main.cpp stores it as one NVS blob.
struct RunLog {
    static const int kMax = 20;
    uint8_t   version = 2;        // bump when RunRecord changes (an old log is then dropped)
    uint8_t   count = 0;
    uint16_t  nextSeq = 1;
    RunRecord runs[kMax];

    void     add(RunRecord r);    // assigns seq
    void     clear();
    uint16_t best(int seg, bool mph = false) const;   // fastest cs of a segment among runs in that unit
};

enum TimerState : uint8_t { TS_NO_SPEED, TS_MOVING, TS_STAGED, TS_RUN, TS_FINISH, TS_SAVED, TS_NO_RESULT };
enum TimerEvent : uint8_t { TE_NONE, TE_ARMED, TE_START, TE_SPLIT, TE_FINISH, TE_SAVED, TE_DISCARD };

class DragTimer {
public:
    static const int kThresholds = 4;
    static constexpr float kThKmh[kThresholds] = { 100, 120, 160, 200 };   // km/h
    static constexpr float kThMph[kThresholds] = { 60, 80, 100, 120 };     // mph
    // Speed units of the splits; takes effect from the next run (never mid-run).
    void  setMph(bool mph);
    bool  mph() const { return mph_; }
    float th(int i) const { return th_[i]; }                              // km/h
    int   thLabel(int i) const { return (int)(mph_ ? kThMph[i] : kThKmh[i]); }   // in the shown unit

    void again();                               // drop any result / run, wait for a stop
    // Call every frame with the newest speed sample (stamp 0 = no data yet). `now` may be a
    // hair older than `stamp` (the sample was published on the other core after `now` was
    // read): that counts as fresh, never as stale.
    TimerEvent update(float kmh, uint32_t stamp, uint32_t now, const RunLog &log);
    // Optional: the throttle channel. While READY, pressing it (>= 10 %) starts the clock.
    TimerEvent throttle(float pct, uint32_t stamp, uint32_t now);

    TimerState state() const { return state_; }
    bool  running() const { return state_ == TS_RUN; }
    bool  hasResult() const { return state_ == TS_FINISH || state_ == TS_SAVED; }
    float elapsed(uint32_t now) const;          // s since launch (live while running)
    float segTime(int seg, uint32_t now) const; // s; live for the active segment; <0 = not started
    bool  segDone(int seg) const;
    bool  segActive(int seg) const;             // being timed right now
    float segAge(int seg, uint32_t now) const;  // s since the segment was stamped, <0 = not yet
    float segProgress(int seg) const;           // 0..1 by speed, for the panel bars
    bool  newBest(int seg) const { return newBest_[seg]; }
    float speed() const { return lastV_; }
    float maxKmh() const { return maxV_; }
    float toMax() const { return (float)((maxT_ - start_) / 1000.0); }   // s, launch -> top speed
    const RunRecord &record() const { return rec_; }

private:
    TimerState state_ = TS_NO_SPEED;
    uint32_t lastStamp_ = 0, lastThrStamp_ = 0;
    float    lastThr_ = 0;
    double   lastT_ = 0;                        // ms of the previous sample
    float    lastV_ = 0;
    double   stillSince_ = -1;                  // ms the car has been stopped since, -1 = moving
    double   zeroT_ = 0;                        // last sample at standstill before launch
    double   start_ = 0;                        // launch time (ms)
    double   firstT_ = 0; float firstV_ = 0;    // first moving sample (start refinement)
    bool     refined_ = false;
    double   cross_[kThresholds] = {};
    bool     crossed_[kThresholds] = {};
    float    maxV_ = 0;
    double   maxT_ = 0;                         // when the top speed was reached
    bool     newBest_[SEG_COUNT] = {};
    RunRecord rec_ = {};
    bool     mph_ = false, wantMph_ = false;
    float    th_[kThresholds] = { 100, 120, 160, 200 };
    void     applyUnits();

    void       begin(double t);
    TimerEvent end(const RunLog &log);
    double     segBegin(int seg) const;         // ms, <0 = not yet
    double     segEnd(int seg) const;
};
