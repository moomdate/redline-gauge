#pragma once
// OBD-II via an ELM327 Bluetooth Classic adapter (the common blue "OBDII" dongles).
//
// Runs as a simple blocking state machine inside the data task:
//   BT start -> pair/connect -> ELM init (ATZ, ATE0 …) -> find ECU (0100) -> poll PIDs
// RPM is polled every other request so the shift bar stays responsive; standard
// engine PIDs and optional hybrid PIDs fill the remaining slots.
// Any failure drops back a step and retries; the status bar shows where it is.
#include "data/source.h"
#include <stdint.h>

class ObdSource : public DataSource {
public:
    const char *name() const override { return "OBD BT"; }
    void begin() override;
    void end() override;
    void poll() override;
    bool linkStatus(LinkStatus &out) override;

    // Serial command tail after "obd=": "scan", "AA:BB:CC:DD:EE:FF" or "pin=0000".
    // Saved to NVS; safe to call from another core. Returns false if not understood.
    static bool command(const char *arg);

    // One device found by a Bluetooth scan (for the on-screen pair modal).
    struct BtDevice { char name[24]; char mac[18]; int16_t rssi; bool isObd; };
    static const int kMaxScan = 12;
    // UI-driven scan, used while OBD BT is the active source (the stack is already up).
    // requestScan() asks the data task to scan; scanState() = 0 idle / 1 scanning / 2 done;
    // scanResults() copies the list; selectDevice() pairs with one (same as obd=<mac>).
    // All safe to call from the UI core.
    static void requestScan();
    static int  scanState();
    static int  scanResults(BtDevice *out, int max);
    static void selectDevice(const char *mac);

    // Drag timer open: poll speed every other request instead of ~1 in 6.
    static volatile bool fastSpeed;

    // The three right-hand panels' items (PanelItem each). Only what they show is polled (RPM, speed and the
    // hybrid values always are); until this is called everything is. Safe to call from another core.
    static void setShownItems(const uint8_t items[3]);
    // Panel items this car can fill (bit n = PanelItem n), known once the ECU search is done; 0 before that.
    static uint32_t supportedItems();

    static constexpr uint8_t kMaxPids = 24;   // size of the per-PID tables (static_assert in the .cpp)

private:
    enum State : uint8_t { S_BT_START, S_CONNECT, S_INIT, S_SEARCH, S_RUN, S_WAIT };
    State    state_ = S_BT_START;
    State    after_ = S_CONNECT;     // state to resume after S_WAIT
    uint32_t waitUntil_ = 0;
    bool     btStarted_ = false;
    uint8_t  slot_ = 0;              // position in the list of non-RPM requests
    bool     rpmTurn_ = false;       // the next request is RPM
    uint8_t  errors_ = 0;
    uint8_t  noData_[kMaxPids] = {}; // per-PID "NO DATA" counter, >=3 = unsupported
    char     resp_[160];
    char     mac_[18] = {};          // adapter to connect to ("" = scan)
    char     pin_[17] = {};           // fixed PIN ("" = cycle the common ones)
    uint8_t  pinIdx_ = 0;
    uint8_t  macFails_ = 0;
    uint8_t  nodataRun_ = 0;         // consecutive NO DATA replies
    uint32_t pidMask_[5] = {};       // supported PIDs 01-A0 (0100, 0120 … 0180), [0] = 0: unknown

    void retryIn(uint32_t ms, State then, const char *msg);
    bool elm(const char *cmd, uint32_t timeoutMs);
    void loadTarget();
    bool scan(char *macOut);
    int  scanInto(BtDevice *out, int max);   // fill the list; returns count
    bool usable(int idx) const;
    void forgetBond(const char *mac);
    bool readPid(int idx);
};
