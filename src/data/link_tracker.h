#pragma once
// Keeps a source's LinkStatus up to date from its data task and hands copies to the UI task.
// One per source; the source calls connecting() / live() / reply() / retry() as it goes.
#include "data/link_status.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

class LinkTracker {
public:
    LinkTracker(const char *title, const char *subtitle) {
        snprintf(st_.title, sizeof st_.title, "%s", title);
        snprintf(st_.subtitle, sizeof st_.subtitle, "%s", subtitle);
    }

    template <typename F> void edit(F f) {
        portENTER_CRITICAL(&mux_);
        f(st_);
        portEXIT_CRITICAL(&mux_);
    }

    void copy(LinkStatus &out) {
        portENTER_CRITICAL(&mux_);
        out = st_;
        portEXIT_CRITICAL(&mux_);
    }

    void start() {                     // the source just became active
        edit([](LinkStatus &s) {
            LinkStatus fresh;
            memcpy(fresh.title, s.title, sizeof fresh.title);
            memcpy(fresh.subtitle, s.subtitle, sizeof fresh.subtitle);
            s = fresh;
            s.state = LS_CONNECTING;
        });
        n_ = 0;
        rateAt_ = millis();
    }

    void stop() { edit([](LinkStatus &s) { s.state = LS_OFF; }); }

    void connecting(const char *step) {
        edit([&](LinkStatus &s) {
            if (s.state == LS_LIVE) s.drops++;
            s.state = LS_CONNECTING;
            snprintf(s.step, sizeof s.step, "%s", step);
            s.reason[0] = 0;
        });
    }

    void live() {
        uint32_t now = millis();
        edit([&](LinkStatus &s) {
            if (s.state != LS_LIVE) { s.connects++; s.liveSince = now; }
            s.state = LS_LIVE;
            s.reason[0] = 0;
        });
    }

    void reply() {                     // a valid reply arrived
        uint32_t now = millis();
        n_++;
        bool roll = now - rateAt_ >= 1000;
        uint16_t r = roll ? (uint16_t)(n_ * 1000UL / (now - rateAt_)) : 0;
        if (roll) { n_ = 0; rateAt_ = now; }
        edit([&](LinkStatus &s) {
            s.lastReply = now;
            if (roll) s.rate = r;
        });
    }

    void retry(const char *reason, const char *hint, uint32_t inMs) {
        uint32_t at = millis() + inMs;
        edit([&](LinkStatus &s) {
            if (s.state == LS_LIVE) s.drops++;
            s.state = LS_RETRY;
            snprintf(s.reason, sizeof s.reason, "%s", reason);
            snprintf(s.hint, sizeof s.hint, "%s", hint);
            s.retryAt = at;
            s.rate = 0;
        });
    }

private:
    LinkStatus   st_;
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    uint16_t     n_ = 0;
    uint32_t     rateAt_ = 0;
};
