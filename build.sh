#!/bin/bash
# Build out/ikemen.nro (Ikemen GO for the Nintendo Switch),
# out/nxconfig.nro (settings, import and update app) and out/nxbrowser.nro
# (download browser). Run ./setup.sh once before.
#
# Each .nro carries its own version in the NACP (the updater compares it
# with versions.ini): the engine's from app/main.c, the apps' from their
# APP_TITLE.
set -eo pipefail
cd "$(dirname "$0")"
source ./env.sh
mkdir -p out
VER=$(grep -o 'Ikemen NX v[0-9.]*' app/main.c | head -1 | sed 's/.*v//')
CFG_VER=$(grep -o 'Ikemen NX Config v[0-9.]*' nxconfig/main.c | head -1 | sed 's/.*v//')
BRW_VER=$(grep -o 'Ikemen NX Browser v[0-9.]*' nxbrowser/main.c | head -1 | sed 's/.*v//')
echo "== Ikemen NX $VER, Config $CFG_VER, Browser $BRW_VER"

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
aarch64-none-elf-gcc $NX_ARCH -O2 -D__SWITCH__ $NX_INC -Iout -Icommon -c app/main.c -o out/main.o
aarch64-none-elf-gcc $NX_ARCH -specs="$NX_SPECS" -Wl,--wrap=threadCreate \
	-Wl,--wrap=av_get_random_seed -Wl,--wrap=av_random_bytes \
	-Wl,-Map,out/ikemen.map out/main.o out/libikemen.a out/libnxgo.a deps/libxmp/libxmp.a \
	$NX_LIBDIRS \
	-lcurl -lSDL2 -lavfilter -lavformat -lavcodec -lswscale -lswresample -lavutil -lpostproc -lass \
	-lharfbuzz -lfreetype -lfribidi -lpng16 -ldav1d -lbz2 -lz \
	-lEGL -lglapi -ldrm_nouveau -lstdc++ -lnx -lm -o out/ikemen.elf
nacptool --create "Ikemen GO" "Ikemen NX (port by the community)" "$VER" out/ikemen.nacp
elf2nro out/ikemen.elf out/ikemen.nro --icon=app/icon.jpg --nacp=out/ikemen.nacp

echo "-- nxconfig.nro"
nxconfig/base_romfs.sh "deps/Ikemen_GO-v1.0.0-linux.zip" >/dev/null
aarch64-none-elf-gcc $NX_ARCH -O2 -Wall -Wno-format-truncation -D__SWITCH__ $NX_INC -Icommon \
	-c nxconfig/main.c -o out/nxconfig.o
aarch64-none-elf-gcc $NX_ARCH -specs="$NX_SPECS" out/nxconfig.o $NX_LIBDIRS \
	-lcurl -larchive -lexpat -llzma -llz4 -lzstd -lbz2 -lz -lnx -o out/nxconfig.elf
nacptool --create "Ikemen NX Config" "Ikemen NX" "$CFG_VER" out/nxconfig.nacp
elf2nro out/nxconfig.elf out/nxconfig.nro --icon=nxconfig/icon.jpg --nacp=out/nxconfig.nacp \
	--romfsdir=out/nxconfig-romfs

echo "-- nxbrowser.nro"
aarch64-none-elf-gcc $NX_ARCH -O2 -Wall -Wno-format-truncation -D__SWITCH__ $NX_INC -Icommon \
	-c nxbrowser/main.c -o out/nxbrowser.o
aarch64-none-elf-gcc $NX_ARCH -specs="$NX_SPECS" out/nxbrowser.o $NX_LIBDIRS -lcurl -lz -lnx -o out/nxbrowser.elf
nacptool --create "Ikemen NX Browser" "Ikemen NX" "$BRW_VER" out/nxbrowser.nacp
elf2nro out/nxbrowser.elf out/nxbrowser.nro --icon=nxbrowser/icon.jpg --nacp=out/nxbrowser.nacp

ls -la out/*.nro
