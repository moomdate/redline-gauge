#pragma once
// The one place live values flow through. Any data source (simulator, serial,
// OBD, your own sensor code) publishes here; the UI only ever reads snapshots.
// Thread-safe: sources run in their own FreeRTOS task, the UI in loop().
#include <stdint.h>

enum Channel : uint8_t {
    CH_RPM = 0,     // rev/min
    CH_SPEED,       // km/h
    CH_COOLANT,     // °C
    CH_VOLTAGE,     // V
    CH_IAT,         // intake air °C
    CH_GEAR,        // 0 = neutral, 1..n; leave unpublished to let the UI estimate it
    CH_THROTTLE,    // 0..100 %, informational
    CH_HV_SOC,      // hybrid battery state of charge, %
    CH_HV_KW,       // hybrid battery power, kW: + = driving the motor, - = charging / regen
    CH_AFR,         // air-fuel ratio estimated from a narrowband O2 sensor (HONDA K)
    CH_COUNT
};

enum class Link : uint8_t {
    Idle,        // source not started / waiting for first data
    Connecting,  // e.g. Bluetooth pairing, ELM init
    Live,        // real data flowing
    Simulated,   // simulator
    Error,       // adapter missing, ECU not answering …
};

struct GaugeSnapshot {
    float    value[CH_COUNT];
    uint32_t stamp[CH_COUNT];   // millis() of last publish, 0 = never
    Link     link;
    char     linkMsg[12];       // short status for the status bar, "" = none
};

namespace bus {
void     publish(Channel ch, float v);
void     setLink(Link l, const char *msg = "");
void     clear();                          // forget all values (on source switch)
void     snapshot(GaugeSnapshot &out);
uint32_t nowMs();
}
