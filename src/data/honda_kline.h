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
//   -> poll table 0x17: 72 05 71 17 01
#include <stdint.h>
#include <stddef.h>

static const uint8_t HK_PING[] = { 0xFE, 0x04, 0x72, 0x8C };
static const uint8_t HK_INIT[] = { 0x72, 0x05, 0x00, 0xF0, 0x99 };
static const uint8_t HK_TABLE = 0x17;

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

// Only what the gauge shows; the bike's speedo is mechanical, the ECU has no speed.
struct HondaData {
    float rpm, tps, temp, iat, batt;
};

// Table 0x17 on the Wave 110i / 125i ECU (payload = bytes after 02 <len> 71 17):
//   [0-1] rpm  [2] TPS V*256/5  [3] TPS %*2  [4] engine temp sensor V  [5] engine temp +40
//   [6] intake air sensor V  [7] intake air temp +40  [10] battery V*10
//   [11-12] injector  [13] ignition   (the last two are not used)
inline bool hkDecodeMain(const uint8_t *f, size_t n, HondaData &d) {
    if (!hkIsTableReply(f, n, HK_TABLE) || n < 4 + 14 + 1) return false;   // 14 data bytes + checksum
    const uint8_t *p = f + 4;
    d.rpm = (float)(p[0] << 8 | p[1]);
    d.tps = p[3] * 0.5f;
    if (d.tps > 100) d.tps = 100;
    d.temp = p[5] - 40.0f;
    d.iat = p[7] - 40.0f;
    d.batt = p[10] / 10.0f;
    return true;
}
