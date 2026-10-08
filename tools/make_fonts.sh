#!/usr/bin/env bash
# Regenerate every font header in src/fonts/. Needs python3 + Pillow.
#   PY=/path/to/venv/bin/python tools/make_fonts.sh
set -euo pipefail
cd "$(dirname "$0")"
PY="${PY:-python3}"
F=font_src
O=../src/fonts
ASCII=$(printf '%s' ' !"#$%&'"'"'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_`abcdefghijklmnopqrstuvwxyz{|}~')

"$PY" gen_font.py $F/BarlowCondensed-Black.ttf      58 font_rpm   "0123456789,-"            $O/font_rpm.h
"$PY" gen_font.py $F/BarlowCondensed-ExtraBold.ttf  31 font_speed "0123456789-NR"           $O/font_speed.h
"$PY" gen_font.py $F/BarlowCondensed-ExtraBold.ttf  26 font_value "0123456789.-d[] ABCDEFGHIJKLMNOPQRSTUVWXYZ" $O/font_value.h
"$PY" gen_font.py $F/BarlowCondensed-BoldItalic.ttf 12 font_label " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/" $O/font_label.h
"$PY" gen_font.py $F/IBMPlexMono-SemiBold.ttf        9 font_small "${ASCII}°·"              $O/font_small.h
"$PY" gen_font.py $F/BarlowCondensed-ExtraBold.ttf  16 font_ui    " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789%+-/.,:<>" $O/font_ui.h
"$PY" gen_font.py $F/BarlowCondensed-Black.ttf      58 font_timer "0123456789.-"            $O/font_timer.h
"$PY" gen_font.py $F/BarlowCondensed-BlackItalic.ttf 50 font_ready "READY!GO"              $O/font_ready.h
"$PY" gen_font.py $F/BarlowCondensed-BlackItalic.ttf 54 font_title "REDLIN" $O/font_title.h
