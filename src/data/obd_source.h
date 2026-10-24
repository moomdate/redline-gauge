#pragma once
// OBD-II via an ELM327 Bluetooth Classic adapter (the common blue "OBDII" dongles).
//
// Runs as a simple blocking state machine inside the data task:
//   BT start -> pair/connect -> ELM init (ATZ, ATE0 …) -> find ECU (0100) -> poll PIDs
// RPM is polled every other request so the shift bar stays responsive (~5-10 Hz on
// a typical CAN car); speed, coolant, intake temp and battery voltage fill the gaps.
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

    // Drag timer open: poll speed every other request instead of ~1 in 6.
    static volatile bool fastSpeed;

private:
    enum State : uint8_t { S_BT_START, S_CONNECT, S_INIT, S_SEARCH, S_RUN, S_WAIT };
    State    state_ = S_BT_START;
    State    after_ = S_CONNECT;     // state to resume after S_WAIT
    uint32_t waitUntil_ = 0;
    bool     btStarted_ = false;
    uint8_t  slot_ = 0;
    uint8_t  errors_ = 0;
    uint8_t  noData_[8] = {};        // per-PID "NO DATA" counter, >=3 = unsupported
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
    bool usable(int idx) const;
    void forgetBond(const char *mac);
    bool readPid(int idx);
};
