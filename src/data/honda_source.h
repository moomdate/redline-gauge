#pragma once
// Honda Wave 110i / 125i (red 4-pin diagnostic connector, K-line) through a K-line
// transceiver board (L9637D / MC33660 / opto) on CN1:
//   transceiver RX-out -> GPIO 22      CYD GPIO 27 -> transceiver TX-in
//   logic VCC <- 3.3 V (never 5 V on GPIO 22)    bike side: K-line, GND, +12 V switched
// Same blocking state machine style as ObdSource:
//   wake pulse -> ping -> init -> poll table 0x17 (+ the O2 table 0x20 four times a second)
// Shows rpm, throttle, engine temp, battery and the AFR estimated from the O2 sensor.
// Lost link: a new wake-up every 2.5 s; linkStatus() tells the UI where it stands.
// Serial `kdump` toggles a raw dump of every known table once a second (with the bytes
// that changed marked) so table offsets can be checked on a new model.
#include "data/source.h"
#include <stdint.h>

class HondaKSource : public DataSource {
public:
    const char *name() const override { return "HONDA K"; }
    void begin() override;
    void end() override;
    void poll() override;
    bool linkStatus(LinkStatus &out) override;

    static volatile bool ownsUart;     // main.cpp leaves Serial2 alone while true
    static volatile bool dumpOn;       // `kdump` toggles
    // Opto-isolated DIY interfaces invert the line (see docs/wiring/kline-opto-schematic):
    // invert both UART directions and the wake pulse. Saved in NVS; `klineinvert=on|off`.
    static void setInvert(bool on);
    static bool invert();

private:
    enum State : uint8_t { S_WAKE, S_RUN, S_WAIT };
    enum Idle : uint8_t { IDLE_OK, IDLE_BUSY, IDLE_STUCK_LOW };
    State    state_ = S_WAKE, after_ = S_WAKE;
    uint32_t waitUntil_ = 0, lastDump_ = 0;
    uint8_t  errors_ = 0;
    bool     sawEcho_ = false;
    Idle     idle_ = IDLE_OK;          // K-line state seen before the last wake-up
    bool     o2On_ = true;             // false once the ECU proved it has no table 0x20
    uint8_t  o2Miss_ = 0;
    uint32_t lastO2_ = 0;
    uint8_t  resp_[64];
    uint8_t  respLen_ = 0;

    void retryIn(uint32_t ms, State then, const char *msg);
    Idle waitIdle();                   // wait for the K-line to sit idle before a wake-up
    bool wake();                       // pulse + ping + init; true when the ECU answered
    bool transact(const uint8_t *req, uint8_t n, uint32_t timeoutMs = 150);
    void dump();
    void pollO2();
};
