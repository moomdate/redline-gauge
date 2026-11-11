#pragma once
// Honda motorcycle K-line diagnostic protocol ("HDS tables"), pure helpers - no hardware,
// unit-tested in test/test_native. Used by HondaKSource.
//
// 10400 baud 8N1 on one wire (every sent byte is echoed back). Frame:
//   [addr][len incl. checksum][service][data...][checksum]
//   request addr 0x72 (ping 0xFE), reply addr 0x02 (ping reply 0x0E)
//   checksum: all bytes of the frame sum to 0 (mod 256)
// Session (Wave 110i / 125i): wake pulse (K low 70 ms, high 130 ms) -> ping FE 04 72 8C
//   (-> 0E 04 72 7C) -> init 72 05 00 F0 99 (-> 02 04 00 FA; some ECUs stay quiet)
//   -> find the main table (0x17 Wave, 0x11 / 0x10 / 0x13 others), e.g. 72 05 71 17 01,
//      then poll it and the O2 sensor in table 0x20: 72 05 71 20 F8
#include <stdint.h>
#include <stddef.h>

static const uint8_t HK_PING[] = { 0xFE, 0x04, 0x72, 0x8C };
static const uint8_t HK_INIT[] = { 0x72, 0x05, 0x00, 0xF0, 0x99 };
static const uint8_t HK_TABLE = 0x17;
static const uint8_t HK_TABLE_O2 = 0x20;

inline uint8_t hkChecksum(const uint8_t *b, size_t n) {
    uint8_t s = 0;
    for (size_t i = 0; i < n; i++) s += b[i];
    return (uint8_t)(0x100 - s);
}

// 72 05 71 <table> <cs>
inline void hkTableRequest(uint8_t table, uint8_t out[5]) {
    out[0] = 0x72; out[1] = 0x05; out[2] = 0x71; out[3] = table;
    out[4] = hkChecksum(out, 4);
}

// A complete, well-formed reply frame: len byte matches, checksum sums to 0.
inline bool hkFrameOk(const uint8_t *f, size_t n) {
    if (n < 4 || f[1] != n) return false;
    uint8_t s = 0;
    for (size_t i = 0; i < n; i++) s += f[i];
    return s == 0;
}

// Reply to a table read: 02 <len> 71 <table> <payload...> <cs>
inline bool hkIsTableReply(const uint8_t *f, size_t n, uint8_t table) {
    return hkFrameOk(f, n) && n >= 5 && f[0] == 0x02 && f[2] == 0x71 && f[3] == table;
}

// Only what the gauge shows. hasX = false when the ECU sends FF ("no sensor") or the layout has no X.
struct HondaData {
    float rpm, tps, temp, iat, batt, map, speed;
    bool hasTemp, hasIat, hasMap, hasSpeed;
};

// Main live table, tried in this order at connect (the first that answers is kept).
// Wave first asks 0x17 (checked on the bench and on a Wave 100i dump); other models start
// with 0x11, the scooter (Lead / Giorno+ / PCX) table, as the reference gauge does.
static const uint8_t HK_MAIN_ORDER_WAVE[]  = { 0x17, 0x11, 0x10, 0x13 };
static const uint8_t HK_MAIN_ORDER_OTHER[] = { 0x11, 0x10, 0x17, 0x13 };

// Decoding follows the table the ECU answered, not the bike model (docs: MOTORCYCLE_HONDA.md 2).
// D[n] = reply byte n, payload p[k] = D[k+4].
//                 0x11 / 0x10        0x13, 0x17 (D9 != FF)   0x17 with D9 = FF
//   rpm           D4*256+D5          same                    same
//   TPS           D7 x 0.5           same                    same
//   engine temp   D9 - 40            same                    D13 - 40
//   intake air    D11 - 40           same                    none
//   MAP kPa       D13                same                    none
//   battery       D16 / 10           D14 / 10                D14 / 10
//   speed km/h    D17                none                    D19 if the reply is long enough
// FF in a temperature / MAP byte = no sensor. Wave 100i (real dump): D10..D13 = FF -> no IAT / MAP.
inline bool hkDecodeMain(const uint8_t *f, size_t n, uint8_t table, HondaData &d) {
    if (!hkIsTableReply(f, n, table) || n < 4 + 14 + 1) return false;   // 14 data bytes + checksum
    const uint8_t *p = f + 4;
    const size_t len = n - 5;                                            // data bytes
    const bool wide  = table == 0x11 || table == 0x10;
    const bool alt17 = table == 0x17 && p[5] == 0xFF;
    d.rpm = (float)(p[0] << 8 | p[1]);
    d.tps = p[3] * 0.5f;
    if (d.tps > 100) d.tps = 100;
    const uint8_t t = alt17 ? p[9] : p[5];
    d.hasTemp = t != 0xFF;
    d.temp = t - 40.0f;
    d.hasIat = !alt17 && p[7] != 0xFF;
    d.iat = p[7] - 40.0f;
    d.hasMap = !alt17 && p[9] != 0xFF;
    d.map = p[9];
    d.batt = (wide ? p[12] : p[10]) / 10.0f;
    d.hasSpeed = wide || (alt17 && len > 15);
    d.speed = wide ? p[13] : (alt17 && len > 15) ? p[15] : 0;
    return true;
}
inline bool hkDecodeMain(const uint8_t *f, size_t n, HondaData &d) { return hkDecodeMain(f, n, HK_TABLE, d); }

// Table 0x20, O2 sensor: 02 08 71 20 <o2> <fuel trim, 0x80 = 0 %> 00 <cs>
// (a Wave 100i sends exactly these 3 data bytes; a reply of another length is ignored).
inline bool hkDecodeO2(const uint8_t *f, size_t n, uint8_t &o2Raw) {
    if (!hkIsTableReply(f, n, HK_TABLE_O2) || n != 4 + 3 + 1) return false;
    o2Raw = f[4];
    return true;
}

// O2 sensor volts. A Wave 100i at idle swings between 0x12 and 0xC3: 0.07 / 0.76 V on a
// 1 V scale, the range a narrowband sensor works in (on a 5 V scale 0xC3 would be 3.8 V).
inline float hkO2Volts(uint8_t raw) { return raw / 256.0f; }

// Air-fuel ratio estimated from a narrowband sensor: it only tells rich from lean around
// 14.7, so this is a rough guide (a real AFR reading needs a wideband sensor).
inline float hkAfrFromO2(float volts) {
    static const float V[]   = { 0.00f, 0.10f, 0.20f, 0.45f, 0.60f, 0.75f, 0.90f, 1.00f };
    static const float AFR[] = { 15.8f, 15.4f, 15.1f, 14.7f, 14.2f, 13.6f, 13.0f, 12.6f };
    const int n = sizeof V / sizeof V[0];
    if (volts <= V[0]) return AFR[0];
    for (int i = 1; i < n; i++)
        if (volts <= V[i]) return AFR[i - 1] + (volts - V[i - 1]) / (V[i] - V[i - 1]) * (AFR[i] - AFR[i - 1]);
    return AFR[n - 1];
}
