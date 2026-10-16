# Build environment for Ikemen NX. Sourced by the other scripts.
# Needs devkitPro (devkitA64 + libnx + the switch-* portlibs, see README).
NX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# The devkitPro installer sets DEVKITPRO=/opt/devkitpro, which only exists
# inside its own MSYS2: fall back to the usual install folders.
if [ ! -d "$DEVKITPRO/libnx" ]; then
	for d in /opt/devkitpro /c/devkitPro /c/devkitpro; do [ -d "$d/libnx" ] && DEVKITPRO=$d && break; done
fi
export DEVKITPRO
if [ ! -d "$DEVKITPRO/libnx" ]; then
	echo "devkitPro with libnx not found (set DEVKITPRO)" >&2
	return 1 2>/dev/null || exit 1
fi

# Paths handed to gcc and go must be native on Windows (C:/...).
if command -v cygpath >/dev/null 2>&1; then
	DKP=$(cygpath -m "$DEVKITPRO"); ROOTW=$(cygpath -m "$NX_ROOT"); EXE=.exe
else
	DKP=$DEVKITPRO; ROOTW=$NX_ROOT; EXE=
fi
export DKP ROOTW EXE NX_ROOT

export PATH="$NX_ROOT/goroot/bin:$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"
export GOROOT=$ROOTW/goroot
export GOOS=linux GOARCH=arm64 CGO_ENABLED=1 GOTOOLCHAIN=local
export CC=aarch64-none-elf-gcc CXX=aarch64-none-elf-g++
export NX_ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
export NX_INC="-I$DKP/libnx/include -I$DKP/portlibs/switch/include"
export NX_LIBDIRS="-L$DKP/portlibs/switch/lib -L$DKP/libnx/lib"
export NX_SPECS="$DKP/libnx/switch.specs"
export NX_PORTLIBS_INCLUDE="$DKP/portlibs/switch/include"
export CGO_CFLAGS="$NX_ARCH -O2 -D__SWITCH__ $NX_INC -I$ROOTW/nxcompat/include -I$ROOTW/deps/libxmp/include -DLIBXMP_STATIC"
export CGO_LDFLAGS=""
export PKG_CONFIG=$ROOTW/tools/bin/pkg-config$EXE

# Keep every cache and temporary file inside the repository.
mkdir -p "$NX_ROOT/tmp"
export TMP=$ROOTW/tmp TEMP=$ROOTW/tmp TMPDIR=$ROOTW/tmp
export GOPATH=$ROOTW/gopath GOMODCACHE=$ROOTW/gopath/pkg/mod GOCACHE=$ROOTW/gocache
