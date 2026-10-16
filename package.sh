#!/bin/bash
# Assemble the SD card package: out/ikemen-nx-<version>-sd.zip with
#   switch/ikemen/    ikemen.nro + Ikemen GO v1.0.0 data (default screenpack,
#                     sample characters and stages) + Switch config.ini
#   switch/nxconfig/  nxconfig.nro
# Run ./build.sh first.
set -eo pipefail
cd "$(dirname "$0")"
ROOT=$PWD
IKEMEN_TAG=v1.0.0
ZIP=deps/Ikemen_GO-$IKEMEN_TAG-linux.zip
VER=$(grep -o 'Ikemen NX v[0-9.]*' app/main.c | head -1 | sed 's/.*v//')
[ -f out/ikemen.nro ] && [ -f out/nxconfig.nro ] || { echo "run ./build.sh first" >&2; exit 1; }

if [ ! -f "$ZIP" ]; then
	mkdir -p deps
	curl -sSL -o "$ZIP" \
		https://github.com/ikemen-engine/Ikemen-GO/releases/download/$IKEMEN_TAG/Ikemen_GO-$IKEMEN_TAG-linux.zip
fi

SD=out/sd
rm -rf "$SD" && mkdir -p "$SD/switch/ikemen/save" "$SD/switch/nxconfig" tmp/release
rm -rf tmp/release/* && unzip -q "$ZIP" -d tmp/release
REL=tmp/release
[ -d "$REL/data" ] || REL=$(dirname "$(find tmp/release -maxdepth 3 -type d -name data | head -1)")
for d in data external font sound chars stages video LICENSES.txt README.md; do
	[ -e "$REL/$d" ] && cp -r "$REL/$d" "$SD/switch/ikemen/"
done
mkdir -p "$SD/switch/ikemen/sound"
cp out/ikemen.nro "$SD/switch/ikemen/"
cp sd/config.ini "$SD/switch/ikemen/save/config.ini"
cp out/nxconfig.nro "$SD/switch/nxconfig/"

OUTZIP=out/ikemen-nx-v$VER-sd.zip
rm -f "$OUTZIP"
if command -v zip >/dev/null 2>&1; then
	(cd "$SD" && zip -qr "$ROOT/$OUTZIP" switch)
else
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w tools/zipdir.ps1)" \
		-Base "$(cygpath -w "$SD")" -Entry switch -Out "$(cygpath -w "$ROOT/$OUTZIP")"
fi
ls -la "$OUTZIP"
