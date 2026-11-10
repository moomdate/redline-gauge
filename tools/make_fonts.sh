#!/usr/bin/env bash
# Regenerate every font header in src/fonts/. Needs python3 + Pillow.
#   PY=/path/to/venv/bin/python tools/make_fonts.sh
set -euo pipefail
cd "$(dirname "$0")"
PY="${PY:-python3}"
F=font_src
O=../src/fonts
ASCII=$(printf '%s' ' !"#$%&'"'"'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_`abcdefghijklmnopqrstuvwxyz{|}~')

# REDLINE main font: Kanit (SIL OFL). Kanit is not condensed like Barlow, so the
# large number fonts use a slightly smaller px size to keep digits inside their art slots.
"$PY" gen_font.py $F/Kanit-Black.ttf        50 font_rpm   "0123456789,-"            $O/font_rpm.h
"$PY" gen_font.py $F/Kanit-Bold.ttf         27 font_speed "0123456789-NR"           $O/font_speed.h
"$PY" gen_font.py $F/Kanit-Bold.ttf         23 font_value "0123456789.-d[] ABCDEFGHIJKLMNOPQRSTUVWXYZ" $O/font_value.h
"$PY" gen_font.py $F/Kanit-SemiBold.ttf     12 font_label " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/" $O/font_label.h
"$PY" gen_font.py $F/IBMPlexMono-SemiBold.ttf 9 font_small "${ASCII}°·"              $O/font_small.h
"$PY" gen_font.py $F/Kanit-Bold.ttf         16 font_ui    " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789%+-/.,:<>" $O/font_ui.h
"$PY" gen_font.py $F/Kanit-Black.ttf        50 font_timer "0123456789.-"            $O/font_timer.h
"$PY" gen_font.py $F/Kanit-BlackItalic.ttf  48 font_ready "READY!GO"                $O/font_ready.h
"$PY" gen_font.py $F/Kanit-BlackItalic.ttf  50 font_title "REDLIN"                  $O/font_title.h

# Thai UI font (phase 2). Kanit covers the full Thai block; gen_font.py keeps the
# combining marks at advance 0 so canvas.cpp stacks them over the base consonant.
THAI="กขฃคฅฆงจฉชซฌญฎฏฐฑฒณดตถทธนบปผฝพฟภมยรฤลฦวศษสหฬอฮฯะัาำิีึืฺุู฿เแโใไๅๆ็่้๊๋์ํ๏๐๑๒๓๔๕๖๗๘๙"
"$PY" gen_font.py $F/Kanit-SemiBold.ttf     18 font_th " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,:/%-°${THAI}" $O/font_th.h
