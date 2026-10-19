#!/bin/bash
# Builds out/nxconfig-romfs/base: the clean Ikemen GO v1.0.0 files that
# "Crear juego nuevo" copies into a new game (engine data, the default
# "ikemen1" screenpack, fonts, scripts, intro video, Kung Fu Man and one
# stage). Usage: nxconfig/base_romfs.sh <release zip>
set -eo pipefail
cd "$(dirname "$0")/.."
ZIP=$1
OUT=out/nxconfig-romfs/base
rm -rf out/nxconfig-romfs tmp/base-release
mkdir -p "$OUT/chars" "$OUT/stages" "$OUT/sound" tmp/base-release
unzip -q "$ZIP" -d tmp/base-release
REL=tmp/base-release
[ -d "$REL/data" ] || REL=$(dirname "$(find tmp/base-release -maxdepth 3 -type d -name data | head -1)")
cp -r "$REL/data" "$REL/external" "$REL/font" "$REL/video" "$REL/LICENSES.txt" "$OUT/"
cp -r "$REL/chars/kfm" "$OUT/chars/"
cp "$REL/stages/stage0.def" "$REL/stages/stage0.sff" "$OUT/stages/"
cp nxconfig/base_select.def "$OUT/data/select.def"
rm -rf tmp/base-release
du -sh "$OUT"
