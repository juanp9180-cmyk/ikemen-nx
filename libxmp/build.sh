#!/bin/bash
# Build libxmp (static) for the Nintendo Switch with devkitA64.
# Usage: libxmp/build.sh <version>   (sources in deps/src/libxmp-<version>)
set -eo pipefail
cd "$(dirname "$0")/.."
source ./env.sh
VER=${1:-4.7.4}
OUT=$NX_ROOT/deps/libxmp
mkdir -p "$OUT/obj" "$OUT/include"
cd "deps/src/libxmp-$VER"
CFLAGS="$NX_ARCH -O2 -ffunction-sections -D__SWITCH__ -DLIBXMP_STATIC -DHAVE_POWF -DHAVE_LOCALTIME_R -DHAVE_UNISTD_H -DHAVE_DIRENT_H -Iinclude -Isrc -w"
for f in $(grep -oE 'src/[A-Za-z0-9_/.-]+[.]c' cmake/libxmp-sources.cmake | grep -v '^src/lite' | sort -u); do
	o=$OUT/obj/$(echo "$f" | tr '/' '_' | sed 's/\.c$/.o/')
	aarch64-none-elf-gcc $CFLAGS -c "$f" -o "$o"
done
rm -f "$OUT/libxmp.a"
aarch64-none-elf-ar rcs "$OUT/libxmp.a" "$OUT"/obj/*.o
cp include/xmp.h "$OUT/include/"
echo "libxmp: $OUT/libxmp.a"
