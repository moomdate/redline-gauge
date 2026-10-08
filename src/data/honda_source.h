#pragma once
// Honda Wave 110i / 125i (red 4-pin diagnostic connector, K-line) through a K-line
// transceiver board (L9637D / MC33660 / opto) on CN1:
//   transceiver RX-out -> GPIO 22      CYD GPIO 27 -> transceiver TX-in
//   logic VCC <- 3.3 V (never 5 V on GPIO 22)    bike side: K-line, GND, +12 V switched
// Same blocking state machine style as ObdSource:
//   wake pulse -> ping -> init -> poll table 0x17
// Shows rpm, throttle, engine temp and battery; the Wave has no ECU speed.
// Serial `kdump` toggles a raw dump of every known table once a second (with the bytes
// that changed marked) so table offsets can be checked on a new model.
#include "data/source.h"
#include <stdint.h>

class HondaKSource : public DataSource {
public:
    // test = HONDA TEST: same link, but every 5 s the SPEED slot shows a different raw byte of
    // table 0x17 (status bar "A d[4]", "B d[5]" ...) so a tester can say which one is speed;
    // INTAKE shows d[5]-40 next to COOLANT d[7]-40 to tell the two temperatures apart.
    explicit HondaKSource(bool test = false) : test_(test) {}
    const char *name() const override { return test_ ? "HONDA TEST" : "HONDA K"; }
    void begin() override;
    void end() override;
    void poll() override;

    static volatile bool ownsUart;     // main.cpp leaves Serial2 alone while true
    static volatile bool dumpOn;       // `kdump` toggles
    // Opto-isolated DIY interfaces invert the line (see docs/wiring/kline-opto-schematic):
    // invert both UART directions and the wake pulse. Saved in NVS; `klineinvert=on|off`.
    static void setInvert(bool on);
    static bool invert();

private:
    enum State : uint8_t { S_WAKE, S_RUN, S_WAIT };
    State    state_ = S_WAKE, after_ = S_WAKE;
    uint32_t waitUntil_ = 0, lastDump_ = 0;
    uint8_t  errors_ = 0;
    bool     sawEcho_ = false;
    const bool test_;
    int8_t   testStep_ = -1;           // candidate shown last (-1 = none yet)
    uint8_t  resp_[64];
    uint8_t  respLen_ = 0;

    void retryIn(uint32_t ms, State then, const char *msg);
    bool wake();                       // pulse + ping + init; true when the ECU answered
    bool transact(const uint8_t *req, uint8_t n, uint32_t timeoutMs = 150);
    void dump();
    void publishTest();
};
