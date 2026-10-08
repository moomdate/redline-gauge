# Notes: reading K-line cars without an ELM327 (future "OBD K-LINE" source)

Research notes, October 2026. Nothing here is implemented yet.

## muki01/OBD2_KLine_Library (v2.0.0, commit 2906123)

https://github.com/muki01/OBD2_KLine_Library

**Protocols and features**
- ISO 9141-2 (5-baud init, header `68 6A F1`) and ISO 14230 / KWP2000 (fast init `C1 33 F1 81`, header `C0 33 F1`).
  It tries fast init first; equal keywords mean ISO 9141.
- Manufacturer protocols: VAG KW1281, BMW DS2 and Opel KW82.
- No CAN (the author has a separate library for that).
- OBD services: live data (mode 01, PIDs 0x01–0x63; 0x5B yes, 0x9A no), freeze frame, DTCs (03/07, clear 04),
  vehicle info (09, VIN), supported-PID scan.
- 10400 baud on any ESP32 HardwareSerial. Needs an external transceiver (L9637D, MC33290 …).

**Behaviour to keep in mind**
- Echo skip: discards as many bytes as were sent, with a 100 ms timeout.
- Frames end on a 20 ms (P1) gap, then the checksum is checked.
- No TesterPresent keep-alive; the link stays up only while polling.
- Fully blocking: `delay(5500)` bus idle before every init, 200 ms per bit for 5-baud, P2 up to 1 s.
- About 8–10 responses per second in total, so roughly 3 Hz per value when polling RPM, speed and coolant.

**Maturity**: 70 stars, no issues ever filed, no list of tested vehicles, serial-print examples only.

**License: do not link or copy it.**
- It switched to GPL-3.0 on 2026-10-03, and the source headers still say "dual-licensed, all rights reserved".
- REDLINE is PolyForm Noncommercial, which isn't GPL-compatible.
- Re-implement the public ISO 9141 / 14230 behaviour instead.

## Plan for REDLINE

- Use the same transceiver board and pins as HONDA K (GPIO 22 RX / GPIO 27 TX, 3.3 V logic). Reuse `HondaKSource`'s
  `wake()` / `transact()` / echo skip and the `ownsUart` hand-off.
- Init:
  - 5-baud `0x33`: bit-bang TX at 200 ms/bit, read `55 KW1 KW2`, send `~KW2` after 25–50 ms, expect `~0x33`.
  - Or fast init: 25 ms low, 25 ms high, then `C1 33 F1 81 66`.
- Requests: header + `01 <pid>` + mod-256 sum checksum. Decode the payload with the `obd_parse.h` formulas
  (they need a binary-input variant).
- Keep the link alive (P3 max 5 s) and use state-machine waits instead of `delay()`.
- Doesn't help Honda motorcycles: their HDS protocol is already handled by HONDA K.
- Candidate cars, unverified:
  - Honda City/Jazz GD (2002–08) and older Vios/Yaris: ISO 9141 / 14230.
  - Hilux Vigo ~2004–08: KWP2000. Later Vigo moved to CAN, which works with an ELM327 already.
  - Check that OBD pin 7 (K-line) is populated.
