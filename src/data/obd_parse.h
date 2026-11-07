#pragma once
// Pure ELM327 reply parsing — no hardware, unit-tested in test/test_native.
// Replies are expected compacted: upper-case, spaces/CR/LF removed ("410C1AF8").
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"      // OBD_AFR_STOICH

enum ObdResult : uint8_t { OBD_OK, OBD_NODATA, OBD_BAD };

inline int obdHexByte(const char *p) {
    auto h = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (!p[0] || !p[1]) return -1;
    int a = h(p[0]), b = h(p[1]);
    return (a < 0 || b < 0) ? -1 : a * 16 + b;
}

// Standard Mode 01 PIDs used by the gauge. 9A (hybrid battery V/A) has its own parser.
// Several ECUs may answer; the first "41<pid>" wins.
inline ObdResult obdParsePid(const char *resp, uint8_t pid, float &out) {
    char tag[5];
    snprintf(tag, sizeof tag, "41%02X", pid);
    const char *p = strstr(resp, tag);          // data from any ECU wins over another's NO DATA
    if (!p) return strstr(resp, "NODATA") ? OBD_NODATA : OBD_BAD;
    int A = obdHexByte(p + 4);
    if (A < 0) return OBD_BAD;
    switch (pid) {
        case 0x0C: {
            int B = obdHexByte(p + 6);
            if (B < 0) return OBD_BAD;
            out = (A * 256 + B) / 4.0f;
            return OBD_OK;
        }
        case 0x0D: out = (float)A; return OBD_OK;
        case 0x04: out = A * (100.0f / 255.0f); return OBD_OK;       // calculated engine load
        case 0x06:
        case 0x07: out = A * (100.0f / 128.0f) - 100.0f; return OBD_OK;
        case 0x0B: out = (float)A; return OBD_OK;
        case 0x0E: out = A * 0.5f - 64.0f; return OBD_OK;
        case 0x05:
        case 0x0F: out = A - 40.0f; return OBD_OK;
        case 0x11: out = A * (100.0f / 255.0f); return OBD_OK;
        case 0x14:
            if (obdHexByte(p + 6) < 0) return OBD_BAD;
            out = A * 5.0f;
            return OBD_OK;
        case 0x2F: out = A * (100.0f / 255.0f); return OBD_OK;
        case 0x34: {                                    // 41 34 A B C D: lambda = (A*256+B)/32768 -> AFR
            int B = obdHexByte(p + 6);                  // (C D = sensor current, not used)
            if (B < 0) return OBD_BAD;
            out = (A * 256 + B) / 32768.0f * OBD_AFR_STOICH;
            return OBD_OK;
        }
        case 0x5E: {
            int B = obdHexByte(p + 6);
            if (B < 0) return OBD_BAD;
            out = (A * 256 + B) * 0.05f;
            return OBD_OK;
        }
        case 0x5B: out = A * 100.0f / 255.0f; return OBD_OK;
        default:   return OBD_BAD;
    }
}

// ATRV reply, e.g. "13.9V". Accepts 5..20 V.
inline ObdResult obdParseVolt(const char *resp, float &out) {
    float v = (float)atof(resp);
    if (v <= 5 || v >= 20) return OBD_BAD;
    out = v;
    return OBD_OK;
}

// ---- adapter selection (Bluetooth scan) ---------------------------------------------
// "AA:BB:CC:DD:EE:FF" (either case).
inline bool obdValidMac(const char *m) {
    if (!m || strlen(m) != 17) return false;
    for (int i = 0; i < 17; i++) {
        char c = m[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (i % 3 == 2 ? c != ':' : !hex) return false;
    }
    return true;
}

// Does a Bluetooth name look like an ELM327-type adapter? `preferred` (OBD_BT_NAME)
// matches exactly; otherwise a known brand/keyword anywhere in the name, case-insensitive.
// Kept specific on purpose: a bare "ELM" would also match "HELMET" intercoms, which
// riders very likely have nearby.
inline bool obdNameLooksLikeAdapter(const char *name, const char *preferred) {
    if (!name || !*name) return false;
    char up[64];
    size_t n = 0;
    for (; name[n] && n < sizeof(up) - 1; n++) {
        char c = name[n];
        up[n] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    up[n] = 0;
    if (preferred && *preferred && !strcasecmp(name, preferred)) return true;
    static const char *const kHints[] = {
        "OBD", "ELM327", "ELM 327", "V-LINK", "VLINK", "VEEPEAK", "KONNWEI", "VGATE",
        "ICAR", "CARISTA", "KIWI",
    };
    for (const char *h : kHints) if (strstr(up, h)) return true;
    return false;
}

// PID 9A, hybrid/EV battery: 41 9A A B C D E F. Voltage = (C*256+D)/64 V,
// current = signed (E*256+F)/10 A (SAE J1979-DA). Multi-frame on most cars: the
// caller must strip the "0:" / "1:" frame numbers first (ObdSource::elm does).
inline ObdResult obdParseHybrid(const char *resp, float &volts, float &amps) {
    const char *p = strstr(resp, "419A");
    if (!p) return strstr(resp, "NODATA") ? OBD_NODATA : OBD_BAD;
    int b[6];
    for (int i = 0; i < 6; i++)
        if ((b[i] = obdHexByte(p + 4 + i * 2)) < 0) return OBD_BAD;
    volts = (b[2] * 256 + b[3]) / 64.0f;
    amps = (int16_t)(uint16_t)(b[4] * 256 + b[5]) / 10.0f;
    return OBD_OK;
}

// Supported-PID bitmask of one page: "4100BE3EB811" -> 0xBE3EB811 (base 0x00: bit 31 =
// PID 01). Base 0x20 reads "4120…" (PIDs 21-40), 0x40 "4140…" and so on.
// Several ECUs may answer; their masks are OR-ed. 0 = no valid reply.
inline uint32_t obdSupportedPidsPage(const char *resp, uint8_t base) {
    char tag[5];
    snprintf(tag, sizeof tag, "41%02X", base);
    uint32_t mask = 0;
    for (const char *p = strstr(resp, tag); p; p = strstr(p + 4, tag)) {
        uint32_t m = 0;
        int i = 0;
        for (; i < 4; i++) {
            int b = obdHexByte(p + 4 + i * 2);
            if (b < 0) break;
            m = (m << 8) | (uint32_t)b;
        }
        if (i == 4) mask |= m;
    }
    return mask;
}
inline uint32_t obdSupportedPids(const char *resp) { return obdSupportedPidsPage(resp, 0x00); }
inline bool obdPidSupported(uint32_t mask, uint8_t pid, uint8_t base = 0x00) {
    return pid > base && pid <= base + 0x20 && ((mask >> (32 - (pid - base))) & 1);
}
