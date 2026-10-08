# Arduino examples: send values to REDLINE over serial

| Sketch | What it does |
|---|---|
| [`SerialSenderDemo`](SerialSenderDemo/SerialSenderDemo.ino) | Animated test values, no sensors. **Start here** to check the wiring |
| [`SerialSenderSensors`](SerialSenderSensors/SerialSenderSensors.ino) | Real sensors on an Uno/Nano: RPM (ignition pulse), speed (VSS), battery, coolant/oil NTC, intake NTC |
| [`HondaEcuSim`](HondaEcuSim/HondaEcuSim.ino) | Pretends to be a Honda Wave ECU on the K-line, to bench-test **HONDA K** mode (see [below](#hondaecusim-bench-test-honda-k)) |

Both are plain Arduino sketches: open them in the Arduino IDE, or build with PlatformIO
(`pio ci --board uno examples/SerialSenderDemo/SerialSenderDemo.ino`).
CI compiles them for Uno, Nano, Mega, Leonardo and ESP32.

## Wiring

![Arduino Uno / Nano to CYD wiring](../docs/wiring/wiring-arduino-serial.svg)

The gauge listens on a **receive-only serial input on GPIO 27** (the CN1 connector),
so the CYD's USB port stays free for flashing and logs. 115200 baud, 8N1.

```
 3.3 V board (ESP32, Pico, Nano 33 …)          5 V board (Uno, Nano, Mega, Pro Mini 5V)

   TX ───────────────► GPIO 27 (CN1)             TX ──[ 1k ]──┬──────► GPIO 27 (CN1)
   GND ──────────────► GND                                    [ 2k ]
                                                              │
                                                 GND ─────────┴──────► GND
```

- Leonardo / Pro Micro: the TX pin is `Serial1` (the sketches pick it automatically).
- **Only TX → GPIO 27 and GND are needed.** The gauge never talks back.
- On 5 V boards the 1k/2k divider brings 5 V down to 3.3 V. Don't skip it, or the ESP32 pin may be damaged.
- Power: each board can have its own supply, but **GND must be shared**.
- Changing the pin or baud rate: `EXT_SERIAL_RX_PIN` / `EXT_SERIAL_BAUD` in `src/config.h`
  (`-1` disables it and frees GPIO 27).
- The USB serial port still accepts the same lines, which is handy for testing from a PC (`tools/serial_feed.py`).

## Protocol

One line per update, `key=value` pairs separated by spaces, ending with `\n`:

```
rpm=3200 spd=86 clt=87 volt=13.9 iat=42 gear=3
```

| key (aliases) | unit |
|---|---|
| `rpm` | rev/min |
| `spd` (`speed`, `kmh`) | km/h |
| `clt` (`coolant`, `ect`) | °C |
| `volt` (`v`, `voltage`, `batt`) | V |
| `iat` (`intake`) | °C |
| `gear` | 0 = N, 1..n (leave out to let the gauge estimate it) |
| `tps` (`throttle`) | % |
| `soc` (`hvsoc`) | hybrid battery % (shown with PANEL HYB / AUTO) |
| `kw` (`hvkw`) | hybrid battery kW, negative = regen |

- JSON works too: `{"rpm":3200,"speed":86}`
- Send only what you have. A value not refreshed for **2.5 s** shows `--`.
- 10–20 lines per second is plenty; the gauge smooths RPM between updates.
- `mode=serial` switches the gauge to the SERIAL source. Both sketches send it once at start-up.
  Other commands work as well: `theme=lime`, `shift=6500`, `peak=reset`.

## HondaEcuSim: bench-test HONDA K

Answers the gauge's K-line ping, init and table 0x17 with animated rpm / throttle / engine temp / battery,
at 10400 baud. No K-line board needed: wire the two UARTs directly.

```
 sim TX ──(1k/2k divider on 5 V boards)──► CYD GPIO 22   (K-line RX)
 sim RX ◄──────────────────────────────── CYD GPIO 27   (K-line TX)
 GND ──────────────────────────────────── GND
```

- ESP32: Serial2 on GPIO 16 (RX) / 17 (TX), log on USB. Uno / Nano: D0 / D1 (close the Serial Monitor).
  Mega / Leonardo: Serial1.
- On the gauge: SETUP → DATA SOURCE → **HONDA** (or `mode=honda`), `klineinvert=off`.
- It echoes every byte like the real single wire; `ECHO 0` in the sketch turns that off.

---

# ภาษาไทย: ใช้ Arduino ส่งค่าเข้า REDLINE ผ่าน Serial

| Sketch | ใช้ทำอะไร |
|---|---|
| `SerialSenderDemo` | ส่งค่าทดสอบวิ่งขึ้นลง ไม่ต้องต่อเซ็นเซอร์ **ใช้ลองว่าต่อสายถูกไหมก่อน** |
| `SerialSenderSensors` | อ่านเซ็นเซอร์จริงด้วย Uno/Nano: รอบ (พัลส์จุดระเบิด), ความเร็ว, แบต, อุณหภูมิ NTC |
| `HondaEcuSim` | จำลองเป็นกล่อง ECU ของ Wave บนสาย K-line ใช้ทดสอบโหมด **HONDA K** บนโต๊ะ ไม่ต้องมีรถ |

## ต่อสาย

![ต่อ Arduino Uno / Nano เข้า CYD](../docs/wiring/wiring-arduino-serial.svg)
- ขา **TX ของ Arduino → GPIO 27 ของ CYD** (ช่อง CN1) และ **GND → GND** ใช้แค่ 2 เส้นนี้
- บอร์ด **5V** (Uno, Nano, Mega) **ต้องมีตัวต้านทานแบ่งแรงดัน**: TX → 1k → จุดกลาง → 2k → GND แล้วต่อจุดกลางเข้า GPIO 27
  (ลด 5V เหลือ 3.3V ถ้าไม่ใส่อาจทำให้ขา ESP32 เสีย)
- บอร์ด **3.3V** (ESP32, Pico) ต่อตรงได้เลย
- ใช้แหล่งจ่ายไฟแยกกันได้ แต่ **GND ต้องต่อถึงกัน**
- ช่อง USB ของ CYD ยังว่างไว้ใช้แฟลช/ดู log ได้ตามปกติ

## รูปแบบข้อมูล
ส่งทีละบรรทัด ลงท้ายด้วย `\n` เช่น `rpm=3200 spd=86 clt=87 volt=13.9 iat=42`
- ส่งเฉพาะค่าที่มีได้ ค่าไหนไม่อัปเดตเกิน 2.5 วินาทีจะขึ้น `--`
- ส่ง 10–20 ครั้งต่อวินาทีก็พอ
- ตอนเริ่ม sketch จะส่ง `mode=serial` ให้หน้าปัดสลับมาโหมด SERIAL เอง

## ตั้งค่าใน SerialSenderSensors
- เปิด/ปิดเซ็นเซอร์ที่ `USE_RPM`, `USE_SPEED`, `USE_BATTERY`, `USE_COOLANT`, `USE_INTAKE`
- `PULSES_PER_REV`: จำนวนพัลส์ต่อรอบเครื่อง (รถ 4 สูบ = 2, มอเตอร์ไซค์สูบเดียวอย่าง Wave ส่วนใหญ่ = 1)
- `WHEEL_CIRCUMFERENCE_M`: เส้นรอบวงล้อ (รถเก๋ง ≈ 1.94 ม., Wave ขอบ 17 ≈ 1.75 ม.)
- ⚠️ สัญญาณรอบต้องผ่าน **opto-coupler (PC817)** เสมอ ห้ามต่อสายคอยล์เข้าบอร์ดตรงๆ

## HondaEcuSim: ทดสอบโหมด HONDA K โดยไม่ต้องมีรถ
- ตอบ ping, init และตาราง 0x17 ด้วยค่ารอบ / คันเร่ง / ความร้อน / แบต ที่ขยับเอง (10400 baud)
- ไม่ต้องใช้บอร์ด K-line ต่อ UART ตรง:
  - **TX ของบอร์ดจำลอง → GPIO 22** ของ CYD (บอร์ด 5V ต้องมีตัวแบ่งแรงดัน 1k/2k)
  - **GPIO 27** ของ CYD → **RX ของบอร์ดจำลอง**
  - GND → GND
- ESP32 ใช้ขา 16 (RX) / 17 (TX) · Uno/Nano ใช้ D0/D1 (ปิด Serial Monitor ก่อน) · Mega/Leonardo ใช้ Serial1
- ที่ CYD: SETUP → DATA SOURCE → **HONDA** และ `klineinvert=off`
