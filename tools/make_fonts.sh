#!/usr/bin/env bash
# Regenerate every font header in src/fonts/. Needs python3 + Pillow.
#   PY=/path/to/venv/bin/python tools/make_fonts.sh
set -euo pipefail
cd "$(dirname "$0")"
PY="${PY:-python3}"
F=font_src
O=../src/fonts
ASCII=$(printf '%s' ' !"#$%&'"'"'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_`abcdefghijklmnopqrstuvwxyz{|}~')

# REDLINE Latin/number fonts: Barlow Condensed — tall, narrow, "racing" digits. Kanit
# (added for Thai) renders digits rounder and wider, so the numbers stay on Barlow.
"$PY" gen_font.py $F/BarlowCondensed-Black.ttf      58 font_rpm   "0123456789,-"            $O/font_rpm.h
"$PY" gen_font.py $F/BarlowCondensed-ExtraBold.ttf  31 font_speed "0123456789-NR"           $O/font_speed.h
"$PY" gen_font.py $F/BarlowCondensed-ExtraBold.ttf  26 font_value "0123456789.-d[] ABCDEFGHIJKLMNOPQRSTUVWXYZ" $O/font_value.h
"$PY" gen_font.py $F/BarlowCondensed-BoldItalic.ttf 12 font_label " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/" $O/font_label.h
"$PY" gen_font.py $F/IBMPlexMono-SemiBold.ttf        9 font_small "${ASCII}°·"              $O/font_small.h
"$PY" gen_font.py $F/BarlowCondensed-ExtraBold.ttf  16 font_ui    " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789%+-/.,:<>" $O/font_ui.h
"$PY" gen_font.py $F/BarlowCondensed-Black.ttf      58 font_timer "0123456789.-"            $O/font_timer.h
"$PY" gen_font.py $F/BarlowCondensed-BlackItalic.ttf 50 font_ready "READY!GO"              $O/font_ready.h
"$PY" gen_font.py $F/BarlowCondensed-BlackItalic.ttf 54 font_title "REDLIN"                $O/font_title.h

# Thai UI fonts. Kanit covers the full Thai block; gen_font.py keeps the combining marks at
# advance 0 so canvas.cpp stacks them over the base consonant. Two cuts: 18px for headings
# (settings title) and 12px for compact captions (gauge labels), picked by i18n::font().
THAI="กขฃคฅฆงจฉชซฌญฎฏฐฑฒณดตถทธนบปผฝพฟภมยรฤลฦวศษสหฬอฮฯะัาำิีึืฺุู฿เแโใไๅๆ็่้๊๋์ํ๏๐๑๒๓๔๕๖๗๘๙"
LATIN=" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,:/%-°"
"$PY" gen_font.py $F/Kanit-SemiBold.ttf     18 font_th       "${LATIN}${THAI}" $O/font_th.h
"$PY" gen_font.py $F/Kanit-SemiBold.ttf     12 font_th_small "${LATIN}${THAI}" $O/font_th_small.h
