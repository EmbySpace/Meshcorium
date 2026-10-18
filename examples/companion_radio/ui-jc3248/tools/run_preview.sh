#!/bin/sh
# Regenerates assets, builds the PC preview and renders all pages to PNG.
#   sh tools/run_preview.sh [output dir]
set -e
cd "$(dirname "$0")"
OUT="${1:-$TEMP/jc_preview}"
mkdir -p "$OUT"
python -I gen_assets.py
python -m ziglang c++ -O2 -std=c++17 -w -I.. -I../../../tx_test preview.cpp ../../../tx_test/TxTestUI.cpp ../UIGfx.cpp ../UIScreens.cpp ../UIStyle.cpp ../UIChats.cpp ../UIInterfaces.cpp ../UIRepeater.cpp -o "$OUT/preview.exe"
"$OUT/preview.exe" "$OUT"
python -I -c "
import glob, os, sys
from PIL import Image
for f in glob.glob(os.path.join(sys.argv[1], '*.bmp')):
    Image.open(f).save(f[:-4] + '.png'); os.remove(f)
" "$OUT"
echo "PNGs in $OUT"
