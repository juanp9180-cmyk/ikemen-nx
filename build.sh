#!/bin/bash
# Build out/ikemen.nro (Ikemen GO for the Nintendo Switch) and
# out/nxconfig.nro (settings and import app) and out/nxbrowser.nro (download
# browser). Run ./setup.sh once before.
set -eo pipefail
cd "$(dirname "$0")"
source ./env.sh
mkdir -p out
VER=$(grep -o 'Ikemen NX v[0-9.]*' app/main.c | head -1 | sed 's/.*v//')
echo "== Ikemen NX $VER"

echo "-- horizon layer (libnxgo.a)"
for f in horizon nxdl; do
	aarch64-none-elf-gcc $NX_ARCH -O2 -Wall -ffunction-sections -D__SWITCH__ $NX_INC \
		-c horizon/$f.c -o out/$f.o
done
rm -f out/libnxgo.a
aarch64-none-elf-ar rcs out/libnxgo.a out/horizon.o out/nxdl.o

echo "-- Ikemen GO (Go c-archive, takes a while the first time)"
(cd ikemen && GOEXPERIMENT=arenas go build -tags "nx gles2 netgo osusergo" \
	-buildmode=c-archive -o ../out/libikemen.a ./src)

echo "-- ikemen.nro"
aarch64-none-elf-gcc $NX_ARCH -O2 -D__SWITCH__ $NX_INC -Iout -c app/main.c -o out/main.o
aarch64-none-elf-gcc $NX_ARCH -specs="$NX_SPECS" -Wl,--wrap=threadCreate \
	-Wl,--wrap=av_get_random_seed -Wl,--wrap=av_random_bytes \
	-Wl,-Map,out/ikemen.map out/main.o out/libikemen.a out/libnxgo.a deps/libxmp/libxmp.a \
	$NX_LIBDIRS \
	-lSDL2 -lavfilter -lavformat -lavcodec -lswscale -lswresample -lavutil -lpostproc -lass \
	-lharfbuzz -lfreetype -lfribidi -lpng16 -ldav1d -lbz2 -lz \
	-lEGL -lglapi -ldrm_nouveau -lstdc++ -lnx -lm -o out/ikemen.elf
nacptool --create "Ikemen GO" "Ikemen NX (port by the community)" "$VER" out/ikemen.nacp
elf2nro out/ikemen.elf out/ikemen.nro --icon=app/icon.jpg --nacp=out/ikemen.nacp

echo "-- nxconfig.nro"
nxconfig/base_romfs.sh "deps/Ikemen_GO-v1.0.0-linux.zip" >/dev/null
aarch64-none-elf-gcc $NX_ARCH -O2 -Wall -Wno-format-truncation -D__SWITCH__ $NX_INC \
	-c nxconfig/main.c -o out/nxconfig.o
aarch64-none-elf-gcc $NX_ARCH -specs="$NX_SPECS" out/nxconfig.o $NX_LIBDIRS \
	-larchive -lexpat -llzma -llz4 -lzstd -lbz2 -lz -lnx -o out/nxconfig.elf
nacptool --create "Ikemen NX Config" "Ikemen NX" "$VER" out/nxconfig.nacp
elf2nro out/nxconfig.elf out/nxconfig.nro --icon=nxconfig/icon.jpg --nacp=out/nxconfig.nacp \
	--romfsdir=out/nxconfig-romfs

echo "-- nxbrowser.nro"
aarch64-none-elf-gcc $NX_ARCH -O2 -Wall -Wno-format-truncation -D__SWITCH__ $NX_INC \
	-c nxbrowser/main.c -o out/nxbrowser.o
aarch64-none-elf-gcc $NX_ARCH -specs="$NX_SPECS" out/nxbrowser.o $NX_LIBDIRS -lcurl -lz -lnx -o out/nxbrowser.elf
nacptool --create "Ikemen NX Browser" "Ikemen NX" "$VER" out/nxbrowser.nacp
elf2nro out/nxbrowser.elf out/nxbrowser.nro --icon=nxbrowser/icon.jpg --nacp=out/nxbrowser.nacp

ls -la out/*.nro
