#include "data/serial_source.h"
#include "data/gauge_bus.h"
#include "config.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

volatile unsigned long SerialSource::lastRx_ = 0;

struct Key { const char *name; Channel ch; };
static const Key kKeys[] = {
    {"rpm", CH_RPM},
    {"spd", CH_SPEED}, {"speed", CH_SPEED}, {"kmh", CH_SPEED},
    {"clt", CH_COOLANT}, {"coolant", CH_COOLANT}, {"ect", CH_COOLANT},
    {"v", CH_VOLTAGE}, {"volt", CH_VOLTAGE}, {"voltage", CH_VOLTAGE}, {"batt", CH_VOLTAGE},
    {"iat", CH_IAT}, {"intake", CH_IAT},
    {"gear", CH_GEAR},
    {"tps", CH_THROTTLE}, {"throttle", CH_THROTTLE},
    {"fuel", CH_FUEL_LEVEL}, {"fuellevel", CH_FUEL_LEVEL}, {"fuel_level", CH_FUEL_LEVEL},
    {"fuelrate", CH_FUEL_RATE}, {"fuel_rate", CH_FUEL_RATE},
    {"stft", CH_STFT}, {"ltft", CH_LTFT},
    {"map", CH_MAP},
    {"ign", CH_IGNITION}, {"ignition", CH_IGNITION}, {"advance", CH_IGNITION},
    {"o2", CH_O2_MV}, {"o2mv", CH_O2_MV}, {"o2_mv", CH_O2_MV},
    {"load", CH_LOAD},
    {"soc", CH_HV_SOC}, {"hvsoc", CH_HV_SOC},
    {"kw", CH_HV_KW}, {"hvkw", CH_HV_KW},
};

int SerialSource::feed(const char *line, bool publish) {
    int n = 0;
    const char *p = line;
    while (*p) {
        // key: run of letters
        while (*p && !isalpha((unsigned char)*p)) p++;
        char key[12];
        int k = 0;
        while (isalnum((unsigned char)*p) || *p == '_') {
            if (k < (int)sizeof(key) - 1) key[k++] = (char)tolower((unsigned char)*p);
            p++;
        }
        key[k] = 0;
        if (!k) break;
        // separator: = : " or spaces
        while (*p == '=' || *p == ':' || *p == '"' || *p == ' ') p++;
        char *end;
        float v = strtof(p, &end);
        if (end == p) continue;             // key without number, skip
        p = end;
        for (const Key &e : kKeys) {
            if (strcmp(e.name, key) == 0) {
                if (publish) bus::publish(e.ch, v);
                n++;
                break;
            }
        }
    }
    if (n && publish) lastRx_ = bus::nowMs();
    return n;
}

void SerialSource::begin() {
    lastRx_ = 0;
    wasLive_ = false;
    bus::setLink(Link::Connecting, "WAIT DATA");
}

void SerialSource::poll() {
    // Line reading happens in main.cpp; here we only track link health.
    bool live = lastRx_ && (bus::nowMs() - lastRx_) < DATA_STALE_MS;
    if (live != wasLive_) {
        wasLive_ = live;
        if (live) bus::setLink(Link::Live, "");
        else bus::setLink(Link::Connecting, "WAIT DATA");
    }
}
