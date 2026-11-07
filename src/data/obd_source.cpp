#include "data/obd_source.h"
#include "data/gauge_bus.h"
#include "data/obd_parse.h"
#include "data/link_tracker.h"
#include "config.h"
#include <Arduino.h>
#include <BluetoothSerial.h>
#include <Preferences.h>

static BluetoothSerial SerialBT;

// ---- link status for the UI (the status pill and the OBD BT link page) ----------------------------
static LinkTracker s_link("OBD BT", "ELM327 BLUETOOTH");
static volatile bool s_bt = false;           // Bluetooth link to the adapter is up
static char s_adapter[18] = "";              // MAC of the adapter in use (data task writes)
static volatile uint8_t s_pids = 0;          // supported PIDs found at the last ECU search

// ---- UI-driven Bluetooth scan (pair modal) ----
static ObdSource::BtDevice s_scan[ObdSource::kMaxScan];
static int s_scanN = 0;
static volatile int s_scanState = 0;         // 0 idle, 1 scanning, 2 done
static volatile bool s_scanReq = false;
static portMUX_TYPE s_scanLock = portMUX_INITIALIZER_UNLOCKED;

void ObdSource::requestScan() { s_scanState = 1; s_scanReq = true; }
int  ObdSource::scanState() { return s_scanState; }
int  ObdSource::scanResults(BtDevice *out, int max) {
    portENTER_CRITICAL(&s_scanLock);
    int n = s_scanN < max ? s_scanN : max;
    for (int i = 0; i < n; i++) out[i] = s_scan[i];
    portEXIT_CRITICAL(&s_scanLock);
    return n;
}

static const char *hintFor(const char *reason) {
    if (!strcmp(reason, "NO ADAPTER")) return "Adapter not found: plugged in? powered?";
    if (!strcmp(reason, "BT FAIL"))    return "Bluetooth didn't start: restart the gauge";
    if (!strcmp(reason, "BT LOST"))    return "Bluetooth dropped: adapter unplugged?";
    if (!strcmp(reason, "ELM ERROR"))  return "The adapter doesn't answer like an ELM327";
    if (!strcmp(reason, "ECU LOST"))   return "The car stopped answering: ignition off?";
    return "Adapter OK, car silent: ignition ON?";
}

static void connecting(const char *step) {
    bus::setLink(Link::Connecting, step);
    s_link.connecting(step);
}

bool ObdSource::linkStatus(LinkStatus &out) {
    s_link.copy(out);
    auto row = [&](const char *k, const char *v, bool bad) {
        LinkRow &r = out.rows[out.rowCount++];
        snprintf(r.key, sizeof r.key, "%s", k);
        snprintf(r.value, sizeof r.value, "%s", v);
        r.bad = bad;
    };
    row("BLUETOOTH", s_bt ? "CONNECTED" : "--", false);
    size_t n = strlen(s_adapter);
    row("ADAPTER", n ? s_adapter + (n > 8 ? n - 8 : 0) : "--", false);   // last 3 bytes of the MAC
    char v[14];
    if (s_pids) snprintf(v, sizeof v, "%u", (unsigned)s_pids); else snprintf(v, sizeof v, "--");
    row("PIDS", v, false);
    return true;
}

// ---- which adapter ------------------------------------------------------------------
// ELM327 clones go by many Bluetooth names ("OBDII", "OBD2", "V-LINK", "VEEPEAK" …), so
// an exact-name connect missed most of them. Instead: scan, list everything found on
// the serial log, take the first device whose name looks like an OBD adapter, and
// remember its MAC (NVS) so the next boot connects straight away.
// Serial:  obd=scan  forget the saved adapter and scan again
//          obd=AA:BB:CC:DD:EE:FF  use this adapter      obdpin=0000  fixed PIN
// Clones use one of these legacy PINs; without a fixed one (obdpin=) we cycle through
// OBD_BT_PIN first, then the common ones.
static const char *const kCommonPins[] = { "1234", "0000", "6789", "1111" };
static const char *kPins[5];
static int kPinCount = 0;
static void buildPinList() {
    kPinCount = 0;
    kPins[kPinCount++] = OBD_BT_PIN;
    for (const char *p : kCommonPins) if (strcmp(p, OBD_BT_PIN)) kPins[kPinCount++] = p;
}
volatile bool ObdSource::fastSpeed = false;
static volatile uint32_t s_want = 0xFFFFFFFFu;   // PanelItem bits to poll (all, until main.cpp says what is shown)
static volatile uint32_t s_supported = 0;        // PanelItem bits the car can fill (set after the ECU search)
void ObdSource::setShownItems(const uint8_t items[3]) { s_want = obdWantMask(items); }
uint32_t ObdSource::supportedItems() { return s_supported; }
static volatile bool s_retarget = false;    // set by serial commands (core 1)
static volatile bool s_forgetBond = false;  // obd=scan: also drop the pairing key
static volatile uint32_t s_passkey = 1234;  // SSP passkey answer = the PIN being tried

static bool validMac(const char *m) { return obdValidMac(m); }

static int startPinIdx() { return 0; }   // kPins[0] is OBD_BT_PIN

bool ObdSource::command(const char *line) {
    Preferences p;
    if (!p.begin("obd", false)) return false;
    bool ok = true;
    if (!strncasecmp(line, "scan", 4) || !strncasecmp(line, "forget", 6)) {
        p.remove("mac");
        s_forgetBond = true;
        Serial.println("[obd] saved adapter forgotten, scanning on next attempt");
    } else if (validMac(line) || (strlen(line) > 17 && validMac(String(line).substring(0, 17).c_str()))) {
        p.putString("mac", String(line).substring(0, 17));
        Serial.printf("[obd] adapter set to %.17s\n", line);
    } else if (!strncasecmp(line, "pin=", 4) || !strncasecmp(line, "pin:", 4)) {
        String pin = String(line + 4);
        pin.trim();
        if (pin.length() > 16) { p.end(); Serial.println("[obd] PIN is 1-16 characters"); return true; }
        if (pin.length()) p.putString("pin", pin); else p.remove("pin");
        Serial.printf("[obd] PIN %s\n", pin.length() ? pin.c_str() : "auto (1234/0000/6789/1111)");
    } else {
        ok = false;
    }
    p.end();
    if (ok) s_retarget = true;
    return ok;
}

void ObdSource::selectDevice(const char *mac) {
    Preferences p;
    if (p.begin("obd", false)) { p.putString("mac", mac); p.end(); }
    s_retarget = true;                       // poll() reloads the target and reconnects
    Serial.printf("[obd] selected %s (pairing)\n", mac);
}

void ObdSource::loadTarget() {
    Preferences p;
    mac_[0] = pin_[0] = 0;
    if (p.begin("obd", false)) {   // read-write: creates the namespace, no NOT_FOUND log on first use
        if (p.isKey("mac")) p.getString("mac", mac_, sizeof mac_);
        if (p.isKey("pin")) p.getString("pin", pin_, sizeof pin_);
        p.end();
    }
    if (!validMac(mac_) && validMac(OBD_BT_MAC)) strlcpy(mac_, OBD_BT_MAC, sizeof mac_);
    if (!validMac(mac_)) mac_[0] = 0;
}

static void saveMac(const char *mac) {
    Preferences p;
    if (!p.begin("obd", false)) return;
    if (p.getString("mac", "") != mac) p.putString("mac", mac);
    p.end();
}

// Scan ~10 s and return every device found (name, MAC, signal, whether it looks like an OBD
// adapter). Older ELM327 clones (Bluetooth 2.0) don't put their name in the inquiry reply, so
// unnamed devices get an explicit remote-name request before we judge them.
int ObdSource::scanInto(BtDevice *out, int max) {
    connecting("BT SCAN");
    Serial.println("[obd] scanning for Bluetooth Classic devices (10 s)...");
    BTScanResults *r = SerialBT.discover(10240);
    if (!r) { Serial.println("[obd] scan failed"); return 0; }
    int n = r->getCount();
    if (n > 16) n = 16;
    char names[16][32];
    int rssis[16];
    BTAddress addrs[16];
    int unnamedAsked = 0;
    for (int i = 0; i < n; i++) {
        BTAdvertisedDevice *d = r->getDevice(i);
        addrs[i] = d->getAddress();
        rssis[i] = d->haveRSSI() ? d->getRSSI() : -999;
        names[i][0] = 0;
        if (d->haveName()) strlcpy(names[i], d->getName().c_str(), sizeof names[i]);
    }
    for (int i = 0; i < n; i++) {
        if (names[i][0] || unnamedAsked >= 6) continue;      // cap: each try can take ~6 s
        unnamedAsked++;
        char rn[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
        SerialBT.invalidateRemoteName();
        SerialBT.requestRemoteName((uint8_t *)addrs[i].getNative());
        // wait for this device's name, then move on to the NEXT unnamed one (not break: a single
        // clone that won't answer must not stop us naming the rest, e.g. an unnamed V-LINK)
        for (uint32_t t0 = millis(); millis() - t0 < 6000; delay(50))
            if (SerialBT.readRemoteName(rn)) { strlcpy(names[i], rn, sizeof names[i]); break; }
        SerialBT.invalidateRemoteName();
    }
    int cnt = 0;
    for (int i = 0; i < n && cnt < max; i++) {
        bool obd = obdNameLooksLikeAdapter(names[i], OBD_BT_NAME);
        Serial.printf("[obd]   %s  %-20s rssi=%d%s\n", addrs[i].toString(true).c_str(),
                      names[i][0] ? names[i] : "(no name)", rssis[i], obd ? "  <- OBD" : "");
        BtDevice &e = out[cnt++];
        strlcpy(e.name, names[i][0] ? names[i] : "(no name)", sizeof e.name);
        strlcpy(e.mac, addrs[i].toString(true).c_str(), sizeof e.mac);
        e.rssi = (int16_t)rssis[i];
        e.isObd = obd;
    }
    SerialBT.discoverClear();
    return cnt;
}

// Pick the strongest OBD-looking device for the auto-connect flow.
bool ObdSource::scan(char *macOut) {
    BtDevice list[16];
    int n = scanInto(list, 16);
    int best = -1, bestRssi = -1000;
    for (int i = 0; i < n; i++)
        if (list[i].isObd && list[i].rssi > bestRssi) { best = i; bestRssi = list[i].rssi; }
    if (best < 0) {
        Serial.printf("[obd] %d device(s), none named like an OBD adapter.\n"
                      "[obd] Pick yours on screen (BLUETOOTH > PAIR) or type  obd=AA:BB:CC:DD:EE:FF\n"
                      "[obd] Nothing at all? BLE-only, unpowered, or a phone is still connected to it.\n", n);
        return false;
    }
    strlcpy(macOut, list[best].mac, 18);
    return true;
}

// PIDs we read. Index order matters for noData_[] and the schedule below.
enum {
    P_RPM, P_SPEED, P_COOLANT, P_IAT, P_VOLT, P_HV_SOC, P_HV,
    P_TPS, P_FUEL_LEVEL, P_FUEL_RATE, P_STFT, P_LTFT, P_MAP, P_IGNITION, P_O2, P_AFR, P_LOAD, P_COUNT
};
struct PidDef { const char *cmd; uint8_t pid; Channel ch; };
static const PidDef kPids[] = {
    {"010C", 0x0C, CH_RPM},
    {"010D", 0x0D, CH_SPEED},
    {"0105", 0x05, CH_COOLANT},
    {"010F", 0x0F, CH_IAT},
    {"ATRV", 0x00, CH_VOLTAGE},     // adapter's own supply-pin reading = battery voltage
    {"015B", 0x5B, CH_HV_SOC},      // hybrids only (e.g. Honda e:HEV); others: skipped via 0140
    {"019A", 0x9A, CH_HV_KW},       // hybrid battery volts x amps -> kW
    {"0111", 0x11, CH_THROTTLE},
    {"012F", 0x2F, CH_FUEL_LEVEL},
    {"015E", 0x5E, CH_FUEL_RATE},
    {"0106", 0x06, CH_STFT},
    {"0107", 0x07, CH_LTFT},
    {"010B", 0x0B, CH_MAP},
    {"010E", 0x0E, CH_IGNITION},
    {"0114", 0x14, CH_O2_MV},
    {"0134", 0x34, CH_AFR},         // O2 sensor 1 equivalence ratio (lambda) x 14.7 -> AFR
    {"0104", 0x04, CH_LOAD},        // calculated engine load
};
static_assert(sizeof kPids / sizeof kPids[0] == P_COUNT, "kPids / enum mismatch");
static_assert(P_COUNT <= ObdSource::kMaxPids, "ObdSource::noData_ is too small for the PID list");
// The right-hand panel item each PID feeds; PI_COUNT = not a panel value, always polled.
static const uint8_t kPidItem[P_COUNT] = {
    PI_COUNT, PI_COUNT, PI_COOLANT, PI_INTAKE, PI_VOLTAGE, PI_COUNT, PI_COUNT,
    PI_THROTTLE, PI_FUEL_LEVEL, PI_FUEL_RATE, PI_STFT, PI_LTFT, PI_MAP, PI_IGNITION, PI_O2_MV, PI_AFR, PI_LOAD,
};
// RPM is polled on every other request. The requests in between walk through this list; entries the car
// doesn't support or that no right-hand panel shows (see usable()) are skipped, so what IS shown gets the
// slots. Speed and throttle come twice per round.
static const uint8_t kOthers[] = {
    P_SPEED, P_TPS, P_COOLANT, P_IAT, P_VOLT, P_FUEL_LEVEL, P_FUEL_RATE, P_STFT, P_LTFT,
    P_SPEED, P_TPS, P_MAP, P_IGNITION, P_O2, P_AFR, P_LOAD, P_HV, P_HV_SOC,
};

void ObdSource::retryIn(uint32_t ms, State then, const char *msg) {
    bus::setLink(Link::Error, msg);
    if (then == S_CONNECT || then == S_BT_START) s_bt = false;
    s_link.retry(msg, hintFor(msg), ms);
    waitUntil_ = millis() + ms;
    after_ = then;
    state_ = S_WAIT;
}

void ObdSource::begin() {
    state_ = S_BT_START;
    slot_ = 0;
    rpmTurn_ = false;
    buildPinList();
    pinIdx_ = startPinIdx();
    macFails_ = 0;
    nodataRun_ = 0;
    memset(pidMask_, 0, sizeof pidMask_);
    s_retarget = false;
    loadTarget();
    errors_ = 0;
    memset(noData_, 0, sizeof noData_);
    s_link.start();
    s_bt = false;
    s_pids = 0;
    s_supported = 0;
    connecting("BT START");
}

void ObdSource::end() {
    s_link.stop();
    s_bt = false;
    s_supported = 0;
    if (btStarted_) {
        SerialBT.disconnect();
        SerialBT.end();
        btStarted_ = false;
    }
}

// Send a command, collect the reply up to the '>' prompt. The reply is stored
// upper-case with spaces/CR/LF removed, e.g. "410C1AF8". Returns false on timeout.
bool ObdSource::elm(const char *cmd, uint32_t timeoutMs) {
    while (SerialBT.available()) SerialBT.read();
    SerialBT.print(cmd);
    SerialBT.print('\r');
    size_t n = 0, lineStart = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        while (SerialBT.available()) {
            char c = (char)SerialBT.read();
            if (c == '>') { resp_[n] = 0; return true; }
            if (c == '\r' || c == '\n') { lineStart = n; continue; }
            if (c == ' ' || c == 0) continue;
            // multi-frame CAN replies number their lines "0:", "1:" …: drop the numbers so
            // the payload reads as one hex string ("BUS INIT:" etc. are longer, untouched)
            if (c == ':' && n == lineStart + 1) { n = lineStart; continue; }
            if (n < sizeof(resp_) - 1) resp_[n++] = (char)toupper((unsigned char)c);
        }
        delay(2);
    }
    resp_[n] = 0;
    return false;
}

// Returns true if the request got a valid answer (published) or a clean "NO DATA".
bool ObdSource::readPid(int idx) {
    const PidDef &d = kPids[idx];
    if (!elm(d.cmd, OBD_CMD_TIMEOUT_MS)) return false;
    float v;
    if (d.pid == 0) {                                   // ATRV -> "13.9V"
        if (obdParseVolt(resp_, v) == OBD_OK) bus::publish(d.ch, v);
        return true;
    }
    if (d.pid == 0x9A) {
        float volts, amps;
        switch (obdParseHybrid(resp_, volts, amps)) {
            case OBD_OK:     noData_[idx] = 0; nodataRun_ = 0;
                             bus::publish(CH_HV_KW, volts * amps * HV_CURRENT_SIGN / 1000.0f);
                             return true;
            case OBD_NODATA: if (noData_[idx] < 255) noData_[idx]++;
                             return true;
            default:         return false;
        }
    }
    switch (obdParsePid(resp_, d.pid, v)) {
        case OBD_OK:     noData_[idx] = 0; nodataRun_ = 0; bus::publish(d.ch, v); return true;
        case OBD_NODATA: if (noData_[idx] < 255) noData_[idx]++;
                         if (nodataRun_ < 255) nodataRun_++;
                         return true;
        default:         return false;
    }
}

// A stale link key (adapter re-paired with a phone, or it forgot ours) makes every
// connect fail no matter which PIN; dropping the bond forces a fresh pairing.
void ObdSource::forgetBond(const char *mac) {
    BTAddress a{String(mac)};
    if (SerialBT.deleteBondedDevice((uint8_t *)a.getNative()))
        Serial.printf("[obd] pairing with %s removed\n", mac);
}

// Without a 0100 bitmask (some ECUs answer it oddly) fall back to the NO DATA counter.
bool ObdSource::usable(int idx) const {
    if (!obdItemWanted(s_want, kPidItem[idx])) return false;   // no panel shows it
    uint8_t pid = kPids[idx].pid;
    if (pid == 0) return true;                             // ATRV is the adapter itself
    if (pidMask_[0]) {
        int pg = (pid - 1) / 0x20;
        return pg < 5 && obdPidSupported(pidMask_[pg], pid, pg * 0x20);
    }
    return noData_[idx] < 3;
}

void ObdSource::poll() {
    if (s_scanReq && btStarted_) {           // on-screen BLUETOOTH modal asked for a scan
        s_scanReq = false;
        if (SerialBT.connected()) SerialBT.disconnect();     // free the radio for a clean inquiry
        BtDevice list[kMaxScan];
        int n = scanInto(list, kMaxScan);
        portENTER_CRITICAL(&s_scanLock);
        s_scanN = n;
        for (int i = 0; i < n; i++) s_scan[i] = list[i];
        portEXIT_CRITICAL(&s_scanLock);
        s_scanState = 2;
        state_ = S_CONNECT;                  // resume (reconnect, to a new pick if one was made)
    }
    if (s_retarget) {                       // obd=… typed: drop the link, reconnect to the new target
        s_retarget = false;
        if (s_forgetBond && btStarted_ && mac_[0]) forgetBond(mac_);
        s_forgetBond = false;
        loadTarget();
        pinIdx_ = startPinIdx();
        macFails_ = 0;
        if (btStarted_) {
            if (SerialBT.connected()) SerialBT.disconnect();
            state_ = S_CONNECT;
        }
    }
    switch (state_) {
    case S_WAIT:
        if ((int32_t)(millis() - waitUntil_) >= 0) state_ = after_;
        else delay(20);
        break;

    case S_BT_START:
        // master mode; BLE disabled to leave RAM for the display
        // Newer adapters use Secure Simple Pairing: accept "just works"/numeric comparison
        // (we chose this device ourselves) and answer a passkey request with the PIN.
        SerialBT.onConfirmRequest([](uint32_t) { SerialBT.confirmReply(true); });
        SerialBT.onKeyRequest([]() { SerialBT.respondPasskey(s_passkey); });
        if (!SerialBT.begin("REDLINE", true, true)) { retryIn(3000, S_BT_START, "BT FAIL"); break; }
        SerialBT.setPin(OBD_BT_PIN, strlen(OBD_BT_PIN));
        btStarted_ = true;
        state_ = S_CONNECT;
        break;

    case S_CONNECT: {
        // The saved/configured (or just scanned) MAC is retried until every PIN has failed
        // on it, then we scan again (adapter swapped, or it was the wrong device).
        char target[18];
        int maxFails = pin_[0] ? 3 : kPinCount;
        if (mac_[0] && macFails_ < maxFails) {
            strlcpy(target, mac_, sizeof target);
        } else if (scan(target)) {
            strlcpy(mac_, target, sizeof mac_);     // RAM only; saved to NVS once it connects
            macFails_ = 0;
        } else {
            // nothing found: try the saved adapter again next round. Its name may simply not
            // match (set by obd=<MAC>), or it was unpowered while the car was off.
            macFails_ = 0;
            retryIn(3000, S_CONNECT, "NO ADAPTER");
            break;
        }
        const char *pin = pin_[0] ? pin_ : kPins[pinIdx_];
        SerialBT.setPin(pin, strlen(pin));
        s_passkey = strtoul(pin, nullptr, 10);
        connecting("BT PAIRING");
        Serial.printf("[obd] connecting to %s (PIN %s)\n", target, pin);
        bool ok = SerialBT.connect(BTAddress(target)) && SerialBT.connected(5000);
        if (!ok) {
            Serial.println("[obd] connect failed");
            if (!pin_[0]) pinIdx_ = (pinIdx_ + 1) % kPinCount;
            if (macFails_ < 255) macFails_++;
            if (macFails_ == maxFails) forgetBond(target);   // every PIN failed: re-pair from scratch
            retryIn(2000, S_CONNECT, "NO ADAPTER");
            break;
        }
        Serial.printf("[obd] bluetooth connected to %s\n", target);
        saveMac(target);                             // writes only if it changed
        strlcpy(s_adapter, target, sizeof s_adapter);
        s_bt = true;
        macFails_ = 0;
        state_ = S_INIT;
        break;
    }

    case S_INIT: {
        connecting("ELM INIT");
        // reset; reply is the version banner. Slow clones may answer after the timeout, so
        // swallow anything late before ATE0, or its '>' would end the ATE0 read early.
        if (!elm("ATZ", 5000)) {
            for (uint32_t t0 = millis(); millis() - t0 < 500; delay(10))
                while (SerialBT.available()) SerialBT.read();
        }
        Serial.printf("[obd] ATZ -> %s\n", resp_);
        // Cheap "v2.1" mini clones don't implement every AT command and answer "?" to some.
        // Only echo-off must work; the rest are best effort (the parser copes without them:
        // spaces are stripped, and headers/linefeeds don't hide the "41xx" reply).
        if (!elm("ATE0", OBD_CMD_TIMEOUT_MS) || !strstr(resp_, "OK")) {
            Serial.printf("[obd] init failed, ATE0 -> '%s'\n", resp_);
            if (!SerialBT.connected()) { retryIn(2000, S_CONNECT, "BT LOST"); break; }
            retryIn(2000, S_INIT, "ELM ERROR");
            break;
        }
        static const char *const opt[] = {"ATL0", "ATS0", "ATH0", "ATAT1", "ATSP0"};
        for (const char *c : opt) {
            if (!elm(c, OBD_CMD_TIMEOUT_MS) || !strstr(resp_, "OK"))
                Serial.printf("[obd] %s not supported ('%s'), continuing\n", c, resp_);
        }
        state_ = S_SEARCH;
        break;
    }

    case S_SEARCH:
        // First real request makes the ELM auto-detect the protocol ("SEARCHING...").
        connecting("ECU SEARCH");
        if (elm("0100", 10000) && strstr(resp_, "4100")) {
            // The 0100 reply says which PIDs 01-20 this ECU has; poll only those. Per-PID
            // "NO DATA" counts start fresh, so a sleepy ECU doesn't disable a PID for good.
            memset(pidMask_, 0, sizeof pidMask_);
            pidMask_[0] = obdSupportedPids(resp_);
            // follow the chain: the last bit of each page says whether the next page exists
            for (int pg = 1; pg < 5 && (pidMask_[pg - 1] & 1); pg++) {
                char cmd[5];
                snprintf(cmd, sizeof cmd, "01%02X", pg * 0x20);
                if (!elm(cmd, OBD_CMD_TIMEOUT_MS * 2)) break;
                pidMask_[pg] = obdSupportedPidsPage(resp_, pg * 0x20);
            }
            memset(noData_, 0, sizeof noData_);
            nodataRun_ = 0;
            s_supported = obdSupportedItems(pidMask_);
            Serial.printf("[obd] ECU found, PIDs 01-A0: %08lX %08lX %08lX %08lX %08lX\n",
                          (unsigned long)pidMask_[0], (unsigned long)pidMask_[1], (unsigned long)pidMask_[2],
                          (unsigned long)pidMask_[3], (unsigned long)pidMask_[4]);
            bus::setLink(Link::Live, "");
            int pids = 0;
            for (uint32_t m : pidMask_) for (int b = 0; b < 32; b++) pids += (m >> b) & 1;
            s_pids = (uint8_t)(pids > 255 ? 255 : pids);
            s_link.live();
            errors_ = 0;
            state_ = S_RUN;
        } else {
            Serial.printf("[obd] 0100 -> '%s'\n", resp_);
            if (!SerialBT.connected()) retryIn(2000, S_CONNECT, "BT LOST");
            else retryIn(3000, S_SEARCH, "NO ECU");   // ignition off?
        }
        break;

    case S_RUN: {
        // RPM on every other request; between them the next supported, shown entry of kOthers
        int idx = P_RPM;
        static bool speedTurn = false;
        speedTurn = !speedTurn;
        if (fastSpeed && speedTurn) idx = P_SPEED;          // drag timer: speed every other request
        else if (rpmTurn_) rpmTurn_ = false;                // idx stays P_RPM
        else {
            rpmTurn_ = true;
            idx = obdPickOther(kOthers, sizeof kOthers, slot_, [this](int p) { return usable(p); }, P_RPM);
        }
        if (readPid(idx) && nodataRun_ < 12) {
            errors_ = 0;
            bus::setLink(Link::Live, "");
            s_link.reply();
        } else if (nodataRun_ >= 12) {                     // ECU answers nothing but NO DATA
            Serial.println("[obd] only NO DATA replies, searching for the ECU again");
            retryIn(2000, S_SEARCH, "NO ECU");
        } else if (++errors_ >= 6) {
            Serial.printf("[obd] lost ECU, last reply '%s'\n", resp_);
            if (!SerialBT.connected()) retryIn(1000, S_CONNECT, "BT LOST");
            else retryIn(1000, S_SEARCH, "ECU LOST");
        }
        break;
    }
    }
}
