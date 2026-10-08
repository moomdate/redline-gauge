/*
  REDLINE — HondaEcuSim
  ---------------------------------------------------------------------------
  Pretends to be a Honda Wave 110i / 125i ECU on the K-line so the gauge's HONDA K
  mode can be tested on the bench, without a bike or a K-line transceiver board.
  It answers the ping, the init and table 0x17 with animated values:
  rpm (idle -> rev -> idle), throttle, engine temp warming up, battery.

  Wiring (logic level UART, no K-line board in between):
    this board TX  ->  CYD GPIO 22 (CN1)   K-line RX on the gauge
    this board RX  <-  CYD GPIO 27 (CN1)   K-line TX on the gauge
    this board GND ->  CYD GND
    5 V boards (Uno/Nano/Mega/Leonardo): put a divider on TX -> 1k from TX, 2k to GND,
    and take GPIO 22 from the middle (5 V -> 3.3 V). The gauge's 3.3 V TX into this
    board's RX is fine. 3.3 V boards (ESP32 …) connect directly.

  Ports: Uno / Nano use Serial (D0 / D1): close the Serial Monitor and unplug nothing
  else from D0/D1. Mega: Serial1 (TX1 18 / RX1 19). Leonardo / Pro Micro: Serial1.
  ESP32: Serial2 on SIM_RX_PIN / SIM_TX_PIN below, Serial (USB) prints what it does.

  On the gauge: SETUP -> DATA SOURCE -> HONDA (or `mode=honda` over USB), klineinvert=off.
  Like a real K-line the sim echoes every byte it receives (set ECHO 0 to turn it off).

  Copyright (c) 2026 moomdate — PolyForm Noncommercial 1.0.0
*/

#include <stdarg.h>

#if defined(ESP32)
#define SIM_RX_PIN 16                    // <- CYD GPIO 27
#define SIM_TX_PIN 17                    // -> CYD GPIO 22
HardwareSerial &KLINE = Serial2;
#elif defined(USBCON) || defined(HAVE_HWSERIAL1)
HardwareSerial &KLINE = Serial1;         // Leonardo / Pro Micro / Mega
#else
HardwareSerial &KLINE = Serial;          // Uno / Nano: D0 / D1
#endif

// Log to USB on an ESP32 (its K-line is a separate UART); silent elsewhere.
void LOG(const char *fmt, ...) {
#if defined(ESP32)
  char b[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  Serial.print(b);
#else
  (void)fmt;
#endif
}

#define ECHO 1                           // 1 = echo received bytes back, like the single wire

const uint8_t PING_REPLY[] = { 0x0E, 0x04, 0x72, 0x7C };
const uint8_t INIT_REPLY[] = { 0x02, 0x04, 0x00, 0xFA };

uint8_t rx[32];
uint8_t rxLen = 0;

uint8_t checksum(const uint8_t *b, uint8_t n) {
  uint8_t s = 0;
  for (uint8_t i = 0; i < n; i++) s += b[i];
  return (uint8_t)(0x100 - s);
}

void sendFrame(const uint8_t *f, uint8_t n) {
  delay(5);                              // a real ECU answers a few ms after the request
  KLINE.write(f, n);
  KLINE.flush();
}

// Animated engine: 20 s loop of idle, a rev to ~9000, back to idle. Temp warms up over 2 min.
void sendTable17() {
  unsigned long t = millis();
  float phase = (t % 20000UL) / 20000.0f;          // 0..1
  float tps = 0, rpm = 1400;
  if (phase > 0.25f && phase < 0.75f) {
    float x = (phase - 0.25f) / 0.5f;              // 0..1..0 over the rev
    float up = x < 0.5f ? x * 2 : (1 - x) * 2;
    tps = up * 100;
    rpm = 1400 + up * 7600;
  }
  float temp = 30 + 60 * min(1.0f, t / 120000.0f); // 30 -> 90 C
  float batt = 13.9f - rpm / 9000.0f * 0.4f;
  float injMs = 1.8f + tps / 100.0f * 4.0f;
  float ignDeg = 10 + rpm / 9000.0f * 25;

  uint8_t f[4 + 16 + 1] = { 0x02, sizeof f, 0x71, 0x17 };
  uint8_t *d = f + 4;
  uint16_t r = (uint16_t)rpm;
  d[0] = r >> 8;        d[1] = r & 0xFF;
  d[2] = (uint8_t)((0.5f + tps / 100.0f * 4.0f) * 256 / 5);   // TPS volts
  d[3] = (uint8_t)(tps * 2);                                   // TPS % x2
  d[7] = (uint8_t)(temp + 40);                                 // engine temp +40
  d[10] = (uint8_t)(batt * 10);                                // battery V x10
  uint16_t inj = (uint16_t)(injMs * 250);
  d[11] = inj >> 8;     d[12] = inj & 0xFF;                    // injector ms x250
  d[13] = (uint8_t)((ignDeg + 64) * 2);                        // ignition (deg + 64) x2
  f[sizeof f - 1] = checksum(f, sizeof f - 1);
  sendFrame(f, sizeof f);
  LOG("T17 rpm=%u tps=%.0f temp=%.0f batt=%.1f\n", r, tps, temp, batt);
}

void handleFrame(const uint8_t *f, uint8_t n) {
  if (f[0] == 0xFE && n == 4 && f[2] == 0x72) {
    sendFrame(PING_REPLY, sizeof PING_REPLY);
    LOG("ping\n");
  } else if (f[0] == 0x72 && n == 5 && f[2] == 0x00 && f[3] == 0xF0) {
    sendFrame(INIT_REPLY, sizeof INIT_REPLY);
    LOG("init\n");
  } else if (f[0] == 0x72 && n == 5 && f[2] == 0x71 && f[3] == 0x17) {
    sendTable17();
  } else {
    LOG("ignored request %02X %02X %02X %02X\n", f[0], f[1], f[2], n > 3 ? f[3] : 0);  // other tables: silent
  }
}

void setup() {
#if defined(ESP32)
  Serial.begin(115200);
  KLINE.begin(10400, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
#else
  KLINE.begin(10400);
#endif
  LOG("Honda ECU sim ready\n");
}

void loop() {
  while (KLINE.available()) {
    uint8_t b = KLINE.read();
#if ECHO
    KLINE.write(b);
#endif
    // the wake pulse arrives as 0x00 / framing junk: wait for a frame start
    if (rxLen == 0 && b != 0xFE && b != 0x72) continue;
    rx[rxLen++] = b;
    if (rxLen >= 2 && (rx[1] < 4 || rx[1] > sizeof rx)) { rxLen = 0; continue; }
    if (rxLen >= 2 && rxLen == rx[1]) {
      if (checksum(rx, rxLen - 1) == rx[rxLen - 1]) handleFrame(rx, rxLen);
      rxLen = 0;
    }
  }
}
