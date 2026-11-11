#include "data/honda_source.h"
#include "data/honda_kline.h"
#include "data/gauge_bus.h"
#include "data/link_tracker.h"
#include "config.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <Preferences.h>

volatile bool HondaKSource::ownsUart = false;
volatile bool HondaKSource::dumpOn = false;
static volatile bool s_invert = KLINE_INVERT;

// ---- link status for the UI (written by the data task, copied by the UI task) ----
static LinkTracker s_link("K-LINE", "HONDA ECU LINK");
// details for the link page: plain words, single writer (data task)
static volatile bool  s_echo = false, s_hasO2 = false;
static volatile uint8_t s_idle = 0;              // HondaKSource::Idle
static volatile float s_o2Volts = 0, s_afr = 0;

bool HondaKSource::invert() { return s_invert; }

void HondaKSource::setInvert(bool on) {
    s_invert = on;
    Preferences p;
    if (p.begin("kline", false)) { p.putBool("inv", on); p.end(); }
}

static HardwareSerial &K = Serial2;        // HONDA K always uses the UART2 peripheral; only the pins change
static volatile uint8_t s_uart = 2;        // 2 = GPIO22/27, 0 = GPIO3/1 (USB header)
static int s_rxPin = KLINE_RX_PIN, s_txPin = KLINE_TX_PIN;

uint8_t HondaKSource::uart() { return s_uart; }
bool HondaKSource::ownsConsole() { return ownsUart && s_uart == 0; }

void HondaKSource::setUart(uint8_t u) {
    s_uart = (u == 0) ? 0 : 2;
    s_rxPin = s_uart == 0 ? KLINE_UART0_RX_PIN : KLINE_RX_PIN;
    s_txPin = s_uart == 0 ? KLINE_UART0_TX_PIN : KLINE_TX_PIN;
}

void HondaKSource::retryIn(uint32_t ms, State then, const char *msg) {
    bus::setLink(Link::Error, msg);
    s_echo = sawEcho_;
    s_idle = idle_;
    const char *hint = !strcmp(msg, "K-LINE LOW") ? "K-line stays low: check 12 V and wiring"
                     : !strcmp(msg, "NO K-LINE")  ? "No echo: check TX/RX, 3.3 V, 12 V"
                                                  : "Wiring OK, ECU silent: ignition ON?";
    s_link.retry(msg, hint, ms);
    waitUntil_ = millis() + ms;
    after_ = then;
    state_ = S_WAIT;
}

void HondaKSource::begin() {
    ownsUart = true;                   // main.cpp stops reading Serial2 as the text input
    if (s_uart == 0) Serial.end();     // free the USB console (UART0) so K-line can use GPIO3/1
    Preferences p;
    if (p.begin("kline", false)) { s_invert = p.getBool("inv", KLINE_INVERT); p.end(); }
    delay(20);                         // let a poll in progress on the other core finish
    state_ = S_WAKE;
    errors_ = 0;
    sawEcho_ = false;
    bus::setLink(Link::Connecting, "K-LINE INIT");
    s_link.start();
    s_hasO2 = false;
}

void HondaKSource::end() {
    K.end();
    if (s_uart == 0) Serial.begin(SERIAL_BAUD);   // give the USB console back
#if EXT_SERIAL_RX_PIN >= 0
    // hand GPIO 27 back to the text serial input (receive-only), idle-high
    pinMode(KLINE_TX_PIN, INPUT);
    K.begin(EXT_SERIAL_BAUD, SERIAL_8N1, EXT_SERIAL_RX_PIN, -1);
    gpio_pullup_en((gpio_num_t)EXT_SERIAL_RX_PIN);
#endif
    ownsUart = false;
    s_link.stop();
}

static bool readByte(uint8_t &b, uint32_t until) {
    while ((int32_t)(millis() - until) < 0) {
        if (K.available()) { b = (uint8_t)K.read(); return true; }
        delay(1);
    }
    return false;
}

// The K-line before a wake-up, read on the RX pin with the UART off: the bus has to sit idle
// (high) for 300 ms, which also gives the ECU time to boot after the key is turned on.
// A line that never goes high is a wiring / power problem, not a silent ECU.
HondaKSource::Idle HondaKSource::waitIdle() {
    const int idleLevel = s_invert ? LOW : HIGH;
    pinMode(s_rxPin, INPUT_PULLUP);
    for (int attempt = 0; attempt < 5; attempt++) {
        bool sawHigh = false, sawLow = false;
        uint32_t start = millis();
        while (millis() - start < 300) {
            if (digitalRead(s_rxPin) == idleLevel) sawHigh = true;
            else sawLow = true;
            delayMicroseconds(200);
        }
        if (!sawHigh) return IDLE_STUCK_LOW;
        if (!sawLow) return IDLE_OK;
        // traffic on the bus (the ECU still answering an old request): wait for it to stop
    }
    return IDLE_BUSY;
}

// Wake-up: K-line low 70 ms, high 130 ms (a long "break" the UART can't produce on its own),
// then ping and init. Connected = the ECU answered at least one of them with a valid frame
// (the transceiver's echo alone doesn't count: it comes back even with the ignition off).
// A missed init reply is sent again (up to 5 times) before giving up on this wake-up, since
// a single corrupted byte otherwise costs a full retry cycle.
bool HondaKSource::wake() {
    K.end();
    idle_ = waitIdle();
    // "K low" is TX low on a normal transceiver, TX high through an inverting opto stage
    uint8_t kLow = s_invert ? HIGH : LOW, kHigh = s_invert ? LOW : HIGH;
    pinMode(s_txPin, OUTPUT);
    digitalWrite(s_txPin, kLow);
    delay(70);
    digitalWrite(s_txPin, kHigh);
    // start the UART inside the 130 ms high phase, so a pin glitch from K.begin can't land
    // right before the ping
    K.begin(10400, SERIAL_8N1, s_rxPin, s_txPin, s_invert);
    gpio_pullup_en((gpio_num_t)s_rxPin);        // opto boards leave RX open-collector, no pull-up
    delay(130);
    while (K.available()) K.read();
    bool ping = transact(HK_PING, sizeof HK_PING, 300) && resp_[0] == 0x0E;
    bool init = false;
    for (int i = 0; i < 5 && !init; i++) {
        delay(20);                                 // gap between messages
        init = transact(HK_INIT, sizeof HK_INIT, 500) && resp_[0] == 0x02;
    }
    Serial.printf("[honda] idle %s, ping %s, init %s\n",
                  idle_ == IDLE_OK ? "OK" : idle_ == IDLE_BUSY ? "busy" : "STUCK LOW",
                  ping ? "OK" : "no reply", init ? "OK" : "no reply");
    return ping || init;
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

static volatile bool s_preferWave = true;
void HondaKSource::setPreferWave(bool wave) { s_preferWave = wave; }

bool HondaKSource::findTable() {
    const uint8_t *order = s_preferWave ? HK_MAIN_ORDER_WAVE : HK_MAIN_ORDER_OTHER;
    for (int i = 0; i < 4; i++) {
        uint8_t req[5];
        hkTableRequest(order[i], req);
        HondaData d;
        if (transact(req, 5, 300) && hkDecodeMain(resp_, respLen_, order[i], d)) {
            table_ = order[i];
            Serial.printf("[honda] main table 0x%02X (%u bytes)\n", table_, respLen_);
            return true;
        }
    }
    table_ = 0;
    return false;
}

void HondaKSource::poll() {
    switch (state_) {
    case S_WAIT:
        if ((int32_t)(millis() - waitUntil_) >= 0) state_ = after_;
        else delay(20);
        break;

    case S_WAKE: {
        bus::setLink(Link::Connecting, "K-LINE INIT");
        s_link.connecting("WAKE-UP");
        sawEcho_ = false;
        const bool woke = wake();
        if (woke && findTable()) {
            errors_ = 0;
            o2On_ = true;
            o2Miss_ = 0;
            state_ = S_RUN;
            s_echo = sawEcho_;
            s_idle = idle_;
            s_link.live();
        } else {
            if (woke) {
                Serial.println("[honda] ECU woke up but answered none of tables 0x17 / 0x11 / 0x10 / 0x13 "
                               "(`kdump` lists what it does answer)");
                retryIn(2500, S_WAKE, "NO TABLE");
            } else if (idle_ == IDLE_STUCK_LOW && !sawEcho_) {
                Serial.println("[honda] ECU didn't answer (K-line stays low: check 12 V on the board, "
                               "TX/RX swap, transceiver RX -> GPIO 22)");
                retryIn(2500, S_WAKE, "K-LINE LOW");
            } else {
                Serial.printf("[honda] ECU didn't answer (%s)\n",
                              sawEcho_ ? "echo OK: wiring fine, ignition ON?"
                                       : "no echo: check TX/RX swap, 3.3 V, 12 V on the board");
                retryIn(2500, S_WAKE, sawEcho_ ? "NO ECU" : "NO K-LINE");
            }
        }
        break;
    }

    case S_RUN: {
        if (dumpOn && millis() - lastDump_ >= 1000) { dump(); lastDump_ = millis(); }
        uint8_t req[5];
        hkTableRequest(table_, req);
        HondaData d;
        if (transact(req, 5, 300) && hkDecodeMain(resp_, respLen_, table_, d)) {
            lastRpm_ = d.rpm;                             // remembered so pollO2() can gate the AFR
            bus::publish(CH_RPM, d.rpm);
            bus::publish(CH_THROTTLE, d.tps);
            if (d.hasTemp) bus::publish(CH_COOLANT, d.temp);    // FF = no sensor: shows "--", no false alarm
            if (d.hasIat) bus::publish(CH_IAT, d.iat);
            if (d.hasMap) bus::publish(CH_MAP, d.map);
            if (d.hasSpeed) bus::publish(CH_SPEED, d.speed);
            bus::publish(CH_VOLTAGE, d.batt);
            errors_ = 0;
            bus::setLink(Link::Live, "");
            s_link.reply();
            pollO2();
        } else if (++errors_ >= 5) {
            Serial.printf("[honda] no table 0x%02X reply (%s), connecting again\n", table_,
                          sawEcho_ ? "echo OK: wiring fine, ignition ON?" : "no echo: check TX/RX swap");
            retryIn(1500, S_WAKE, sawEcho_ ? "NO ECU" : "NO K-LINE");
        }
        delay(20);
        break;
    }
    }
}

// O2 sensor (table 0x20) every 250 ms, between two main-table reads. An ECU that never
// answers it (3 misses before the first answer) has no O2 table: stop asking. Misses don't
// count against the link - the main table decides when to reconnect.
void HondaKSource::pollO2() {
    if (!o2On_ || millis() - lastO2_ < 250) return;
    lastO2_ = millis();
    uint8_t req[5], raw;
    hkTableRequest(HK_TABLE_O2, req);
    delay(20);                                         // gap between messages
    if (transact(req, 5, 150) && hkDecodeO2(resp_, respLen_, raw)) {
        float volts = hkO2Volts(raw);
        bus::publish(CH_O2_MV, volts * 1000.0f);         // raw sensor volts: always valid to show
        o2Miss_ = 0;
        s_link.reply();
        s_o2Volts = volts;
        s_hasO2 = true;

        // Track the O2 swing over ~4 s. A narrowband sensor only means anything while it is
        // switching about 0.45 V (closed loop); pegged = open loop (cold engine or full throttle).
        if (millis() - o2WinAt_ > 4000) { o2WinAt_ = millis(); o2Min_ = o2Max_ = volts; }
        if (volts < o2Min_) o2Min_ = volts;
        if (volts > o2Max_) o2Max_ = volts;
        bool switching = (o2Max_ - o2Min_) >= 0.08f;

        // Publish AFR only when it is meaningful: above idle and while the sensor is switching.
        // Otherwise leave CH_AFR to go stale so the panel shows "--", not a wrong number.
        if (lastRpm_ >= 500 && switching) {
            float afr = hkAfrFromO2(volts);
            bus::publish(CH_AFR, afr);
            s_afr = afr;
        }
    } else if (++o2Miss_ >= 3) {
        if (!s_hasO2) {
            o2On_ = false;
            Serial.println("[honda] no O2 table 0x20 on this ECU: AFR off");
        }
        o2Miss_ = 0;
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

bool HondaKSource::linkStatus(LinkStatus &out) {
    s_link.copy(out);
    auto row = [&](const char *k, const char *v, bool bad) {
        LinkRow &r = out.rows[out.rowCount++];
        snprintf(r.key, sizeof r.key, "%s", k);
        snprintf(r.value, sizeof r.value, "%s", v);
        r.bad = bad;
    };
    bool tried = out.state != LS_CONNECTING || out.connects;
    row("ECHO", !tried ? "--" : s_echo ? "YES" : "NO", tried && !s_echo);
    row("K-LINE IDLE", s_idle == IDLE_STUCK_LOW ? "STUCK LOW" : s_idle == IDLE_BUSY ? "BUSY" : "OK", s_idle == IDLE_STUCK_LOW);
    char v[14];
    if (s_hasO2) snprintf(v, sizeof v, "%.2f V", (float)s_o2Volts); else snprintf(v, sizeof v, "--");
    row("O2 SENSOR", v, false);
    if (s_hasO2) snprintf(v, sizeof v, "%.1f", (float)s_afr); else snprintf(v, sizeof v, "--");
    row("AFR (EST.)", v, false);
    return true;
}
