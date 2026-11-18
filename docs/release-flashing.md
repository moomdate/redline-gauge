# Flashing REDLINE / วิธีแฟลช REDLINE

Always flash a **factory image at 0x0**, for both first installs and upgrades.
It includes the bootloader, partition table and app, and **resets settings and the run log**.
ลงใหม่หรืออัปเกรด ใช้ **factory ที่ 0x0** ทุกครั้ง ค่าที่ตั้งไว้และประวัติการวิ่งจะถูกล้าง

## Which file? / ใช้ไฟล์ไหน

Read the marking on the screen glass; USB connector count alone does not identify the panel.
ดูรหัสที่ขอบกระจกจอ อย่าเลือกจากจำนวนช่อง USB อย่างเดียว

| Panel / จอ | Start with / เริ่มด้วย |
|---|---|
| ILI9341 — **TPM408-2.8 / H1954 5-4** | `redline-vX.Y.Z-noinvert-spi40-factory.bin` |
| ST7789 — **H685** | `redline-vX.Y.Z-cyd-st7789-spi40-factory.bin` |

**40 MHz is the default.** Some panels distort, tear or shift at 80 MHz.
The H1954 5-4 marking is at the bottom-right edge of the glass. A real-board test confirmed
ILI9341, `noinvert`, 40 MHz: inverted colours with `invert`, a white screen with ST7789, and distortion at 80 MHz.
เริ่มที่ **40 MHz** จอ H1954 5-4 ทดสอบแล้วว่าใช้ ILI9341 / noinvert / 40 MHz

Use `invert` only if colours look like a photo negative. From v1.6.2, **SETUP → COLORS**
also fixes this and saves the choice. The `noinvert-dim` / `invert-dim` variants add BRIGHTNESS control
for boards that support it; choose their `-spi40-factory.bin` files too.
สีกลับเหมือนฟิล์มเนกาทีฟ ให้กด **SETUP → COLORS** หรือใช้ `invert` ส่วนรุ่น `-dim` เพิ่มการหรี่ไฟจอ

ILI9341 40 MHz images exist from **v1.6.5**. ST7789 first shipped in **v1.9.1**, with
40 MHz in **v1.9.2**; use **v1.9.3 or newer** for the ST7789 display fix.
The flasher only downloads a matching factory asset when that release contains it.

## How to flash / วิธีแฟลช (no compiling needed / ไม่ต้องคอมไพล์)

**REDLINE Flasher (easiest):** download `REDLINE-Flasher-windows.exe` from the
[latest release](https://github.com/moomdate/redline-gauge/releases/latest) (macOS / Linux builds are there too).

1. Plug in the board. The flasher picks the CYD's port (CH340); **Refresh** re-scans.
2. Choose the version and panel. Defaults are **noinvert, 40 MHz**. **Local file** accepts a
   factory / merged image built for 0x0, and refuses app-only images even if renamed.
3. Click **Flash**. It always writes at **0x0**, resets settings and the run log, then restarts the board.

ดาวน์โหลดแล้วเปิดโปรแกรมได้เลย
- เสียบบอร์ด โปรแกรมเลือกพอร์ตให้ กด **Refresh** เพื่อค้นหาใหม่
- เลือกเวอร์ชันและจอ ค่าเริ่มต้นคือ **noinvert / 40 MHz**
- **Local file** ใช้ไฟล์ factory / merged สำหรับ 0x0 เท่านั้น
- กด **Flash** ค่าที่ตั้งไว้และประวัติการวิ่งจะถูกล้าง

Windows may show "Windows protected your PC" because the app isn't code-signed: click **More info → Run anyway**.
No COM port? Install the **CH340 driver** (WCH "CH341SER").
Windows อาจขึ้นเตือนเพราะโปรแกรมไม่มีลายเซ็น ให้กด More info → Run anyway · ไม่เห็นพอร์ต: ลงไดรเวอร์ CH340
macOS: `chmod +x REDLINE-Flasher-macos` then right-click → Open the first time (unsigned app).

The flasher also works in a terminal:
`REDLINE-Flasher --cli` (prompts), or `REDLINE-Flasher --port COM5 --variant noinvert --spi 40 --version latest -y`.
Legacy `--mode factory` / `--mode update` arguments are accepted but ignored with a note:
update mode is gone; every flash is factory at 0x0 and resets settings and the run log.

**In the browser (Chrome / Edge):**

1. Open <https://espressif.github.io/esptool-js/> and plug in the board.
2. Click **Connect** and pick the board's port (CH340 / USB serial).
3. Add the matching `*-factory.bin` at address **0x0** (first install or upgrade).
4. Click **Program**, then press RST (or unplug and replug).

**Command line (first install or upgrade):**

```bash
pip install "esptool<5"
esptool.py --chip esp32 write_flash 0x0 redline-vX.Y.Z-noinvert-spi40-factory.bin
# For ST7789 / H685, use redline-vX.Y.Z-cyd-st7789-spi40-factory.bin at the same address.
```

`SHA256SUMS-*.txt` has the checksums.

## Black screen / boot loop / จอดำ รีสตาร์ทวน

An app-only `*-update.bin` written at `0x10000` leaves the old partition table in place.
On a stock board with a 1 MB app partition, REDLINE cannot fit and repeatedly restarts with a black screen.
A confirmed serial log was: `Image length 2372128 doesn't fit in partition length 1048576`.
Recover by flashing the matching **factory image at 0x0**; this replaces the partition table too.
A colour setting cannot fix this boot failure.

ไฟล์ app-only ที่ 0x10000 ไม่เปลี่ยนตารางพาร์ทิชันเดิม ถ้าช่องแอปมีแค่ 1 MB จะจอดำและรีสตาร์ทวน
แก้โดยแฟลช **factory ที่ 0x0** ให้ตรงรุ่นจอ ค่าที่ตั้งไว้และประวัติการวิ่งจะถูกล้าง
