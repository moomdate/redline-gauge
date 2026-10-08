#include "data/honda_source.h"
#include "data/honda_kline.h"
#include "data/gauge_bus.h"
#include "config.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <Preferences.h>

volatile bool HondaKSource::ownsUart = false;
volatile bool HondaKSource::dumpOn = false;
static volatile bool s_invert = KLINE_INVERT;

bool HondaKSource::invert() { return s_invert; }

void HondaKSource::setInvert(bool on) {
    s_invert = on;
    Preferences p;
    if (p.begin("kline", false)) { p.putBool("inv", on); p.end(); }
}

static HardwareSerial &K = Serial2;

void HondaKSource::retryIn(uint32_t ms, State then, const char *msg) {
    bus::setLink(Link::Error, msg);
    waitUntil_ = millis() + ms;
    after_ = then;
    state_ = S_WAIT;
}

void HondaKSource::begin() {
    ownsUart = true;                   // main.cpp stops reading Serial2 as the text input
    Preferences p;
    if (p.begin("kline", false)) { s_invert = p.getBool("inv", KLINE_INVERT); p.end(); }
    delay(20);                         // let a poll in progress on the other core finish
    state_ = S_WAKE;
    errors_ = 0;
    sawEcho_ = false;
    bus::setLink(Link::Connecting, "K-LINE INIT");
}

void HondaKSource::end() {
    K.end();
#if EXT_SERIAL_RX_PIN >= 0
    // hand GPIO 27 back to the text serial input (receive-only), idle-high
    pinMode(KLINE_TX_PIN, INPUT);
    K.begin(EXT_SERIAL_BAUD, SERIAL_8N1, EXT_SERIAL_RX_PIN, -1);
    gpio_pullup_en((gpio_num_t)EXT_SERIAL_RX_PIN);
#endif
    ownsUart = false;
}

// Wake-up: K-line low 70 ms, high 130 ms (a long "break" the UART can't produce on its own),
// then ping and init. Connected = the ECU answered at least one of them with a valid frame
// (the transceiver's echo alone doesn't count: it comes back even with the ignition off).
bool HondaKSource::wake() {
    K.end();
    // "K low" is TX low on a normal transceiver, TX high through an inverting opto stage
    uint8_t kLow = s_invert ? HIGH : LOW, kHigh = s_invert ? LOW : HIGH;
    pinMode(KLINE_TX_PIN, OUTPUT);
    digitalWrite(KLINE_TX_PIN, kLow);
    delay(70);
    digitalWrite(KLINE_TX_PIN, kHigh);
    delay(130);
    K.begin(10400, SERIAL_8N1, KLINE_RX_PIN, KLINE_TX_PIN, s_invert);
    while (K.available()) K.read();
    bool ping = transact(HK_PING, sizeof HK_PING, 200) && resp_[0] == 0x0E;
    bool init = transact(HK_INIT, sizeof HK_INIT, 300) && resp_[0] == 0x02;
    Serial.printf("[honda] ping %s, init %s\n", ping ? "OK" : "no reply", init ? "OK" : "no reply");
    return ping || init;
}

static bool readByte(uint8_t &b, uint32_t until) {
    while ((int32_t)(millis() - until) < 0) {
        if (K.available()) { b = (uint8_t)K.read(); return true; }
        delay(1);
    }
    return false;
}

// Send a frame and read the reply into resp_. The single wire echoes what we send; the
// echo is skipped when it is there (some isolated boards don't echo).
bool HondaKSource::transact(const uint8_t *req, uint8_t n, uint32_t timeoutMs) {
    while (K.available()) K.read();
    K.write(req, n);
    K.flush();                                         // wait until it is on the wire
    uint32_t until = millis() + timeoutMs;
    uint8_t b, got = 0;
    respLen_ = 0;
    // echo: up to n bytes equal to the request; the first mismatch starts the reply
    while (got < n) {
        if (!readByte(b, millis() + 40)) break;
        if (b == req[got]) { got++; continue; }
        resp_[respLen_++] = b;
        break;
    }
    if (got == n) sawEcho_ = true;
    if (!respLen_ && !readByte(resp_[respLen_++], until)) return false;     // address byte
    if (respLen_ < 2 && !readByte(resp_[respLen_++], until)) return false;  // length byte
    uint8_t len = resp_[1];
    if (len < 4 || len > sizeof(resp_)) return false;
    while (respLen_ < len)
        if (!readByte(resp_[respLen_++], until + 50)) return false;
    return hkFrameOk(resp_, respLen_);
}

void HondaKSource::poll() {
    switch (state_) {
    case S_WAIT:
        if ((int32_t)(millis() - waitUntil_) >= 0) state_ = after_;
        else delay(20);
        break;

    case S_WAKE:
        bus::setLink(Link::Connecting, "K-LINE INIT");
        sawEcho_ = false;
        if (wake()) {
            errors_ = 0;
            state_ = S_RUN;
        } else {
            Serial.printf("[honda] ECU didn't answer (%s)\n",
                          sawEcho_ ? "echo OK: wiring fine, ignition ON?"
                                   : "no echo: check TX/RX swap, 3.3 V, 12 V on the board");
            retryIn(2500, S_WAKE, sawEcho_ ? "NO ECU" : "NO K-LINE");
        }
        break;

    case S_RUN: {
        if (dumpOn && millis() - lastDump_ >= 1000) { dump(); lastDump_ = millis(); }
        uint8_t req[5];
        hkTableRequest(HK_TABLE, req);
        HondaData d;
        if (test_ && transact(req, 5, 120) && hkIsTableReply(resp_, respLen_, HK_TABLE)) {
            publishTest();
            errors_ = 0;
        } else if (!test_ && transact(req, 5, 120) && hkDecodeMain(resp_, respLen_, d)) {
            bus::publish(CH_RPM, d.rpm);
            bus::publish(CH_THROTTLE, d.tps);
            bus::publish(CH_COOLANT, d.temp);
            bus::publish(CH_VOLTAGE, d.batt);
            errors_ = 0;
            bus::setLink(Link::Live, "");
        } else if (++errors_ >= 5) {
            Serial.printf("[honda] no table 0x17 reply (%s), connecting again\n",
                          sawEcho_ ? "echo OK: wiring fine, ignition ON?" : "no echo: check TX/RX swap");
            retryIn(3000, S_WAKE, sawEcho_ ? "NO ECU" : "NO K-LINE");
        }
        delay(20);
        break;
    }
    }
}

// HONDA TEST: the known values as usual, plus one candidate byte (d[4], d[5] ... in turn,
// 5 s each) on SPEED, labelled A, B, C ... in the status bar and on serial.
void HondaKSource::publishTest() {
    const uint8_t *p = resp_ + 4;
    int pn = respLen_ - 5;                            // data bytes (without the checksum)
    if (pn >= 2) bus::publish(CH_RPM, (float)(p[0] << 8 | p[1]));
    if (pn > 3) bus::publish(CH_THROTTLE, p[3] * 0.5f > 100 ? 100 : p[3] * 0.5f);
    if (pn > 5) bus::publish(CH_IAT, p[5] - 40.0f);
    if (pn > 7) bus::publish(CH_COOLANT, p[7] - 40.0f);
    if (pn > 10) bus::publish(CH_VOLTAGE, p[10] / 10.0f);

    const int first = 4;                              // candidates: d[4] .. last data byte
    int count = pn - first;
    if (count <= 0) { bus::setLink(Link::Live, "SHORT"); return; }
    if (count > 26) count = 26;
    int step = (int)((millis() / 5000) % count);
    int idx = first + step;
    bus::publish(CH_SPEED, p[idx]);
    char msg[12];
    snprintf(msg, sizeof msg, "%c d[%d]", 'A' + step, idx);
    bus::setLink(Link::Live, msg);
    if (step != testStep_) {
        testStep_ = step;
        Serial.printf("[htest] %s on SPEED (len %d)\n", msg, pn);
    }
}

// Raw dump of every known table, bytes that changed since the last dump in [brackets].
void HondaKSource::dump() {
    static const uint8_t tables[] = { 0x00, 0x10, 0x11, 0x13, 0x17, 0x20, 0x21, 0x60, 0x61,
                                      0x67, 0x70, 0x71, 0xD0, 0xD1 };
    static uint8_t prev[sizeof tables][64];
    static uint8_t prevLen[sizeof tables];
    Serial.println("[kdump] ---");
    for (size_t i = 0; i < sizeof tables; i++) {
        uint8_t req[5];
        hkTableRequest(tables[i], req);
        if (!transact(req, 5)) { Serial.printf("[kdump] T%02X  (no reply)\n", tables[i]); continue; }
        Serial.printf("[kdump] T%02X len=%2u:", tables[i], respLen_);
        for (uint8_t k = 0; k < respLen_; k++) {
            bool changed = prevLen[i] == respLen_ && prev[i][k] != resp_[k];
            Serial.printf(changed ? " [%02X]" : " %02X", resp_[k]);
        }
        Serial.println();
        memcpy(prev[i], resp_, respLen_);
        prevLen[i] = respLen_;
        delay(20);
    }
}
