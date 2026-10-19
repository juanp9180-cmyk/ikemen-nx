#!/bin/bash
# Fetch the upstream sources, apply the Ikemen NX patches and build the
# patched Go toolchain. Run once (or again after updating the patches).
#
# Needs: git, curl, tar, an installed Go >= 1.24 (bootstraps Go 1.27.2) and
# devkitPro (see README). Everything is placed inside this repository:
#   goroot/          Go 1.27.2 + patches/go     (patched toolchain)
#   ikemen/          Ikemen GO v1.0.0 + patches/ikemen
#   deps/go-sdl2/    go-sdl2 + patches/go-sdl2
#   deps/libxmp/     libxmp 4.7.4 built for the Switch
#   tools/bin/       fake pkg-config used by cgo
set -eo pipefail
cd "$(dirname "$0")"
ROOT=$PWD

GO_TAG=go1.27.2
IKEMEN_TAG=v1.0.0
SDL2_VERSION=v0.5.0-alpha.7.0.20250220045537-7f43f67a3a12
LIBXMP_VERSION=4.7.4

GIT_ID=(-c user.name="Ikemen NX" -c user.email="ikemen-nx@users.noreply.github.com")

step() { printf '\n== %s\n' "$*"; }

if [ ! -d goroot ]; then
	step "Go $GO_TAG"
	git clone -q --depth 1 --branch $GO_TAG https://go.googlesource.com/go goroot
	git -C goroot "${GIT_ID[@]}" am -q "$ROOT"/patches/go/*.patch
fi

if [ ! -x goroot/bin/go ] && [ ! -x goroot/bin/go.exe ]; then
	step "building the patched Go toolchain (uses the installed Go to bootstrap)"
	(
		unset GOOS GOARCH GOROOT CGO_ENABLED
		cd goroot/src
		if command -v cygpath >/dev/null 2>&1; then
			# Git Bash sets NoDefaultCurrentDirectoryInExePath: give cmd a path.
			cmd //c ".\make.bat --no-banner"
		else
			./make.bash --no-banner
		fi
	)
fi

if [ ! -d ikemen ]; then
	step "Ikemen GO $IKEMEN_TAG"
	git clone -q --depth 1 --branch $IKEMEN_TAG https://github.com/ikemen-engine/Ikemen-GO ikemen
	git -C ikemen "${GIT_ID[@]}" am -q "$ROOT"/patches/ikemen/*.patch
fi

step "fake pkg-config"
(
	unset GOOS GOARCH
	EXE=; command -v cygpath >/dev/null 2>&1 && EXE=.exe
	cd tools/pkg-config
	CGO_ENABLED=0 GOROOT="$ROOT/goroot" "$ROOT/goroot/bin/go" build -o "$ROOT/tools/bin/pkg-config$EXE" .
)

source ./env.sh

if [ ! -d deps/go-sdl2 ]; then
	step "go-sdl2 $SDL2_VERSION"
	mkdir -p deps
	go mod download github.com/veandco/go-sdl2@$SDL2_VERSION
	cp -r "$NX_ROOT/gopath/pkg/mod/github.com/veandco/go-sdl2@$SDL2_VERSION" deps/go-sdl2
	chmod -R u+w deps/go-sdl2
	(cd deps/go-sdl2 && patch -p1 -s < "$ROOT"/patches/go-sdl2/0001-nx-system_linux.patch)
fi

if [ ! -f deps/libxmp/libxmp.a ]; then
	step "libxmp $LIBXMP_VERSION"
	mkdir -p deps/src
	curl -sSL -o deps/src/libxmp.tar.gz \
		https://github.com/libxmp/libxmp/releases/download/libxmp-$LIBXMP_VERSION/libxmp-$LIBXMP_VERSION.tar.gz
	tar -xzf deps/src/libxmp.tar.gz -C deps/src
	bash libxmp/build.sh "$LIBXMP_VERSION"
fi

RELZIP=deps/Ikemen_GO-$IKEMEN_TAG-linux.zip
if [ ! -f "$RELZIP" ]; then
	step "Ikemen GO $IKEMEN_TAG release data (screenpack, fonts, scripts)"
	curl -sSL -o "$RELZIP" \
		https://github.com/ikemen-engine/Ikemen-GO/releases/download/$IKEMEN_TAG/Ikemen_GO-$IKEMEN_TAG-linux.zip
fi

step "listo: ahora ejecuta ./build.sh"
