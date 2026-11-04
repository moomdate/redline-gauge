#!/usr/bin/env bash
# Build ready-to-flash images for every panel variant into <out_dir> (default: release/).
#
#   redline-vX.Y.Z-<variant>-factory.bin  everything, flash at 0x0 - first install / unknown board.
#                                         Also wipes the saved settings and run log.
#   redline-vX.Y.Z-<variant>-update.bin   app only, flash at 0x10000 - upgrade, keeps settings + log.
#
# Variants (one per env in platformio.ini):
#   invert      esp32dev       panel needs colour inversion (most CYD boards)
#   noinvert    cyd-noinvert   panel shows correct colours as-is
#   invert-dim  esp32dev-dim   like "invert", for boards whose backlight dims (BRIGHTNESS)
#   noinvert-dim cyd-noinvert-dim  like "noinvert", with BRIGHTNESS
#   <any>-spi40  <env>-spi40    the same with the display SPI at 40 MHz (for panels that glitch at 80)
#   cyd-st7789   cyd-st7789     the "2USB" CYD (micro-USB + USB-C) whose panel is ST7789, not ILI9341
#
# Env: PIO (default: pio), PY (python that can run esptool; default: python3).
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-release}"
PIO="${PIO:-pio}"
PY="${PY:-python3}"
VER="$(sed -n 's/^#define REDLINE_VERSION "\(.*\)"/\1/p' src/config.h)"
[ -n "$VER" ] || { echo "REDLINE_VERSION not found in src/config.h" >&2; exit 1; }
mkdir -p "$OUT"

variant() {
    local env="$1" spi=""
    case "$env" in *-spi40) spi="-spi40"; env="${env%-spi40}" ;; esac
    case "$env" in
        esp32dev)         echo "invert$spi" ;;
        cyd-noinvert)     echo "noinvert$spi" ;;
        esp32dev-dim)     echo "invert-dim$spi" ;;
        cyd-noinvert-dim) echo "noinvert-dim$spi" ;;
        *)                echo "$1" ;;
    esac
}

# esptool: the Python module if installed, else the copy PlatformIO ships
esptool() {
    if "$PY" -c "import esptool" 2>/dev/null; then "$PY" -m esptool "$@"
    else "$PY" "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" "$@"; fi
}

for env in $(grep -o '^\[env:[^]]*' platformio.ini | sed 's/\[env://' | grep -v '^native$'); do
    "$PIO" run -e "$env"
    # boot_app0.bin ships with the Arduino core, which PlatformIO downloads on the first build
    BOOT_APP0="$(find "$HOME/.platformio/packages" -path "*framework-arduinoespressif32*/tools/partitions/boot_app0.bin" 2>/dev/null | head -1 || true)"
    [ -n "$BOOT_APP0" ] || { echo "boot_app0.bin not found under ~/.platformio/packages" >&2; exit 1; }
    B=".pio/build/$env"
    N="redline-v$VER-$(variant "$env")"
    cp "$B/firmware.bin" "$OUT/$N-update.bin"
    esptool --chip esp32 merge_bin -o "$OUT/$N-factory.bin" \
        --flash_mode dio --flash_freq 40m --flash_size 4MB \
        0x1000 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0xe000 "$BOOT_APP0" 0x10000 "$B/firmware.bin"
done
( cd "$OUT" && shasum -a 256 redline-v"$VER"-*.bin > SHA256SUMS-v"$VER".txt )
ls -l "$OUT"
