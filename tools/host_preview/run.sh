#!/usr/bin/env bash
# Build + run the host preview. PY must have Pillow (for PNG/GIF conversion).
set -euo pipefail
cd "$(dirname "$0")"
PY="${PY:-python3}"
SRC=../../src
rm -rf out && mkdir -p out
c++ -std=c++17 -O2 -Wall -Wno-unused-function -I "$SRC" preview.cpp \
    "$SRC/ui/canvas.cpp" "$SRC/ui/gauge_ui.cpp" "$SRC/ui/v2/data_wave_ui.cpp" "$SRC/ui/v2/race_code_ui.cpp" "$SRC/ui/gauge_model.cpp" "$SRC/ui/settings_ui.cpp" "$SRC/ui/splash_ui.cpp" "$SRC/core/provenance.cpp" "$SRC/ui/theme.cpp" "$SRC/ui/drag_timer.cpp" "$SRC/ui/timer_ui.cpp" "$SRC/ui/link_ui.cpp" \
    "$SRC/data/gauge_bus.cpp" "$SRC/data/sim_source.cpp" -o out/preview
./out/preview
"$PY" - <<'PYEOF'
import glob
from PIL import Image
for p in glob.glob("out/*.ppm"):
    if "/sim_" not in p:
        Image.open(p).resize((640, 480), Image.NEAREST).save(p[:-4] + ".png")
frames = [Image.open(p).convert("RGB") for p in sorted(glob.glob("out/sim_*.ppm"))]
if frames:
    frames[0].save("out/sim.gif", save_all=True, append_images=frames[1:], duration=200, loop=0)
import os
for p in glob.glob("out/*.ppm"):
    os.remove(p)
print("wrote out/*.png and out/sim.gif")
PYEOF
