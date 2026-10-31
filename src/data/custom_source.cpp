#include "data/custom_source.h"
#include "gauge_input.h"
#include "config.h"

void CustomSource::begin() {
    if (!began_) { mySensorsBegin(); began_ = true; }   // pins/interrupts set up once
    next_ = millis();
    live_ = -1;
    bus::setLink(Link::Connecting, "NO INPUT");
}

void CustomSource::poll() {
    uint32_t now = millis();
    if ((int32_t)(now - next_) < 0) return;
    next_ = now + CUSTOM_READ_MS;

    GaugeInput in;
    mySensorsRead(in);
    const float vals[] = {
        in.rpm, in.speed, in.coolant, in.voltage, in.intake, in.gear, in.throttle,
        in.fuelLevel, in.fuelRate, in.stft, in.ltft, in.map, in.ignition, in.o2Mv
    };
    const Channel chs[] = {
        CH_RPM, CH_SPEED, CH_COOLANT, CH_VOLTAGE, CH_IAT, CH_GEAR, CH_THROTTLE,
        CH_FUEL_LEVEL, CH_FUEL_RATE, CH_STFT, CH_LTFT, CH_MAP, CH_IGNITION, CH_O2_MV
    };
    int n = 0;
    for (int i = 0; i < (int)(sizeof vals / sizeof vals[0]); i++)
        if (!isnan(vals[i])) { bus::publish(chs[i], vals[i]); n++; }

    if (!n) {   // values pushed with gauge::set() from elsewhere (CAN callback, own task) count too
        GaugeSnapshot snap;
        bus::snapshot(snap);
        for (int c = 0; c < CH_COUNT && !n; c++)
            if (snap.stamp[c] && now - snap.stamp[c] < DATA_STALE_MS) n++;
    }
    int8_t live = n > 0;
    if (live != live_) {
        live_ = live;
        if (live) bus::setLink(Link::Live, "");
        else bus::setLink(Link::Connecting, "NO INPUT");   // nothing filled in my_sensors.cpp yet
    }
}
