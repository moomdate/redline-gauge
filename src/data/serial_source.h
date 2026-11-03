#pragma once
// Live values over USB serial — for bench testing with a PC script, or feeding the
// gauge from another microcontroller / a Raspberry Pi / a CAN logger.
//
// One line per update, any order, any subset, separators: space , ; or JSON-ish:
//   rpm=3200 spd=86 clt=87 volt=13.9 iat=42 gear=3
//   {"rpm":3200,"speed":86,"coolant":87,"voltage":13.9,"iat":42}
// Keys (case-insensitive): rpm | spd,speed,kmh | clt,coolant,ect | v,volt,voltage,batt
//                          | iat,intake | gear | tps,throttle | fuel,fuellevel | fuelrate
//                          | stft | ltft | map | ign,ignition | o2,o2mv | load
// See tools/serial_feed.py for a ready-made sender.
#include "data/source.h"

class SerialSource : public DataSource {
public:
    const char *name() const override { return "SERIAL"; }
    void begin() override;
    void poll() override;

    // Parse one line; returns number of values published. Called by the line
    // reader in main.cpp (which also handles mode= commands).
    static int feed(const char *line, bool publish);

private:
    static volatile unsigned long lastRx_;
    bool wasLive_ = false;
};
