# Third-party components

The `.nro` files are linked statically with the projects below. `setup.sh` downloads the exact source versions; devkitPro packages come from <https://github.com/devkitPro/pacman-packages>.

| Component | Version | License | Source |
|---|---|---|---|
| Ikemen GO | v1.0.0 + `patches/ikemen` | MIT | https://github.com/ikemen-engine/Ikemen-GO |
| Go (runtime and standard library) | go1.27.2 + `patches/go` | BSD-3-Clause | https://go.googlesource.com/go |
| go-sdl2 | v0.5.0-alpha.7.0.20250220045537-7f43f67a3a12 + `patches/go-sdl2` | BSD-3-Clause | https://github.com/veandco/go-sdl2 |
| Go modules used by Ikemen GO | see `ikemen/go.sum` | various (MIT, BSD, Apache-2.0) | Go module proxy |
| libxmp | 4.7.4 | MIT | https://github.com/libxmp/libxmp |
| libnx | devkitPro | ISC | https://github.com/switchbrew/libnx |
| SDL2 | devkitPro `switch-sdl2` | zlib | https://github.com/devkitPro/SDL |
| Mesa, libdrm_nouveau | devkitPro `switch-mesa`, `switch-libdrm_nouveau` | MIT | https://github.com/devkitPro/mesa |
| FFmpeg (avcodec, avformat, avfilter, swscale, swresample, avutil, **postproc**) | devkitPro `switch-ffmpeg` | LGPL-2.1+ / **GPL-2.0+ (postproc)** | https://ffmpeg.org |
| libass, HarfBuzz, FreeType, FriBidi, libpng, dav1d, bzip2, zlib | devkitPro `switch-*` | ISC, MIT, FTL, LGPL-2.1, libpng, BSD-2, bzip2, zlib | devkitPro pacman-packages |

Because FFmpeg's `libpostproc` is GPL and the release binary links it statically, the distributed `.nro` as a whole is covered by the GPL. The complete corresponding source is this repository plus the sources listed above.

## Assets in the SD package

`package.sh` copies data from the official Ikemen GO v1.0.0 release (default screenpack, fonts, sample characters and stages). Their licenses are in `switch/ikemen/LICENSES.txt`. Most are CC BY 3.0; Elecbyte's MUGEN font files are CC BY-NC 3.0. The `.nro` icon is the Ikemen GO logo by Cylia Margatroid (CC BY 3.0).
