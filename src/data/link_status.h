#pragma once
// Where a data source's link to the car stands, for the status pill on the gauge and the
// link page (tap the pill). Plain data: a source fills it, the UI only reads copies.
#include <stdint.h>

enum LState : uint8_t {
    LS_OFF,          // source not running
    LS_CONNECTING,   // working on it; `step` says what (e.g. "BT PAIRING")
    LS_LIVE,         // the ECU answers
    LS_RETRY,        // no answer: waiting to try again, `reason` says why
};

struct LinkRow {                   // a source-specific detail on the link page
    char key[14];
    char value[14];
    bool bad;                      // shown in red
};

struct LinkStatus {
    LState   state = LS_OFF;
    char     title[10] = "";       // page title: "K-LINE", "OBD BT"
    char     subtitle[24] = "";    // "HONDA ECU LINK"
    char     step[14] = "";        // LS_CONNECTING: current step
    char     reason[12] = "";      // LS_RETRY: "NO ECU", "BT LOST" ...
    char     hint[48] = "";        // LS_RETRY: what to check
    uint32_t retryAt = 0;          // millis() of the next attempt (LS_RETRY)
    uint32_t liveSince = 0;        // millis() the current link came up
    uint32_t lastReply = 0;        // millis() of the last valid reply, 0 = never
    uint16_t connects = 0;         // links made since the source started
    uint16_t drops = 0;            // live links lost
    uint16_t rate = 0;             // valid replies per second (last full second)
    LinkRow  rows[4] = {};
    uint8_t  rowCount = 0;
};
