# Ikemen NX: Ikemen GO para Nintendo Switch

Port nativo de [Ikemen GO](https://github.com/ikemen-engine/Ikemen-GO) v1.0.0 (el motor de peleas compatible con MUGEN) como homebrew `.nro` para Nintendo Switch con Atmosphère.

*English summary: native Nintendo Switch homebrew port of Ikemen GO v1.0.0. The Go runtime runs on Horizon through a patched linux/arm64 toolchain whose system calls are served by a small libnx layer. SDL2 + OpenGL ES 3.2 (Mesa/nouveau). Download the SD package from Releases, or build it with `setup.sh`, `build.sh` and `package.sh`.*

Estado: corre en consola real (Atmosphère 1.12, firmware 23). Funcionan el menú, la selección de personajes, las peleas, la música y los controles. También funcionan juegos completos de MUGEN 1.1, rosters de cientos de personajes y screenpacks HD.

## Instalación

1. Descarga `ikemen-nx-vX.Y.Z-sd.zip` desde la página de **Releases**.
2. Descomprímelo en la raíz de la microSD. Quedan:
   - `switch/ikemen/`: el motor (`ikemen.nro`) con el screenpack, los personajes y los escenarios de ejemplo de Ikemen GO.
   - `switch/nxconfig/`: **Ikemen NX Config**, una app para cambiar las opciones sin PC.
3. **Si descomprimiste en Windows, quita el bit de archivo a las carpetas.** Horizon trata como archivo cualquier carpeta que tenga el atributo *Archivo* (el juego no arranca o no encuentra sus datos). Para quitarlo:
   - Desde Hekate: menú *Tools*, opción *Fix Archive Bit* (el nombre del submenú cambia según la versión).
   - Desde Windows, con la SD en `E:`:

     ```bat
     attrib -A -R /S /D E:\switch\ikemen\*
     ```
4. Abre el menú de homebrew **en modo título**: mantén **R** mientras abres cualquier juego. Desde el Álbum (modo applet) hay muy poca memoria y las peleas se ven en negro; el `.nro` lo detecta y te avisa.

### Controles (mando)

| Ikemen | Switch |
|---|---|
| a, b, c | A, B, ZR |
| x, y, z | X, Y, R |
| d, w | L, ZL |
| start / menú | + / − |

En casillas con varios personajes (`slot = { ... }` en `select.def`), **L/ZL** cambian de personaje.

## Agregar personajes, escenarios y screenpacks

Se usa igual que Ikemen GO en PC: copia los personajes a `switch/ikemen/chars/`, los escenarios a `switch/ikemen/stages/` y edita `switch/ikemen/data/select.def`. Para otro screenpack, cambia `Motif` en `switch/ikemen/save/config.ini`. Puedes tener varias instalaciones (por ejemplo `switch/mijuego/`, cada una con su propio `.nro`): el motor usa la carpeta desde donde se abre.

Recomendaciones para la Switch:

- **Música:** deja `BGMRAMBuffer = 0` (viene así). Con `1`, cada canción se decodifica entera en RAM (de 100 a 250 MB) y el juego puede cerrarse por falta de memoria.
- **Rosters grandes** (cientos de personajes): usa `BootLoadingMode = 2` para que los retratos carguen en segundo plano. Con pocos personajes, `0` deja la selección más fluida.
- **Juegos completos de MUGEN 1.1:**
  - Cuando una clave se repite en el `system.def`, Ikemen usa la **primera** y MUGEN la **última**. Por ejemplo, un `time = 0` antes de `time = 45` deja la pantalla VS en 0.
  - Revisa también los valores de `fadein.time`/`fadeout.time` exagerados que solo usaba WinMUGEN, como `90000` (25 minutos de fundido).
  - `winquote.textwrap = l` no existe en Ikemen; usa `w`.
- **Personajes de otro juego** dentro de un juego completo con `common1.cns` propio: pueden necesitar su propio `common1.cns.zss` y el mismo `localcoord` que los demás personajes.

## Ikemen NX Config

Abre `switch/nxconfig`, elige una instalación y cambia:

- **Arcade:** rivales por dificultad, jefes finales y qué jefes salen.
- **Roster** (`select.def`):
  - activar o quitar personajes (la casilla queda vacía, así la grilla no se mueve);
  - activar o quitar los miembros de las casillas compartidas;
  - moverlos de casilla y buscarlos por nombre;
  - agregar los que están en `chars/` y no en el roster.
- **Escenarios y música:** activar o quitar escenarios, elegir la música de cada uno entre los archivos de `sound/` y agregar los que están en `stages/` y no en la lista.
- **Reglas:** dificultad de la CPU, vida, tiempo, rounds, velocidad, continues.
- **Video y sonido:** formato 16:9, 4:3 o según el escenario, y volúmenes.
- **Rendimiento:** carga de retratos, música en RAM, límites de efectos.
- **Pantallas** (`system.def` del screenpack):
  - duración de la pantalla VS, pantallas de victoria y de continuar, modo demo;
  - filas y columnas de la grilla;
  - mostrar, ocultar o renombrar los ítems del menú principal.
- **Grilla de selección:** para screenpacks que traen variantes en una carpeta `slots/`.
- **Editor de archivos:** edita línea por línea `config.ini`, `select.def`, `system.def` y los demás `.def` del screenpack con el teclado de la Switch.

Lo que se quita no se borra del archivo: queda comentado (`;nxoff:` / `;nxhide:`) y se puede reactivar. La primera vez que guarda un archivo deja una copia `.bak` del original.

## Problemas

- El registro queda en `switch/ikemen/go_stdout.log`. Si el juego termina con un error, el `.nro` muestra las últimas líneas en pantalla.
- `switch/ikemen/args.txt` (opcional) recibe opciones de línea de comandos de Ikemen, por ejemplo `kfm kfm -p1.ai 8 -p2.ai 8` para una pelea directa. También acepta estas opciones de diagnóstico:
  - `-nxconsolelog`: copia la consola de depuración de Ikemen al log.
  - `-nxheapprofile`: escribe `heap.pprof` con el uso de memoria.

## Compilar

Requisitos:

- **devkitPro** con devkitA64, libnx y estos paquetes:

  ```bash
  dkp-pacman -S switch-dev switch-sdl2 switch-ffmpeg switch-mesa switch-libdrm_nouveau switch-libass switch-harfbuzz switch-freetype switch-libfribidi switch-libpng switch-dav1d switch-bzip2 switch-zlib
  ```

- **Go 1.24 o superior** instalado. Solo se usa para compilar el toolchain parcheado.
- git, curl, tar y unzip. En Windows: Git Bash o el MSYS2 de devkitPro.

Pasos:

```bash
./setup.sh     # descarga Go 1.27.2, Ikemen GO v1.0.0, go-sdl2 y libxmp, aplica patches/ y compila el toolchain
./build.sh     # out/ikemen.nro y out/nxconfig.nro
./package.sh   # out/ikemen-nx-vX.Y.Z-sd.zip (con los datos oficiales de Ikemen GO v1.0.0)
```

Si devkitPro no está en `/opt/devkitpro` ni en `C:\devkitPro`, define `DEVKITPRO`.

## Cómo funciona

- **Go en Horizon:** Ikemen se compila como `c-archive` para `linux/arm64` con un Go 1.27.2 parcheado (`patches/go`). En vez de `svc` de Linux, el runtime llama a `horizon/horizon.c`, que implementa sobre libnx las llamadas que Go necesita:
  - memoria: un bloque fijo para el heap de Go;
  - hilos, `futex` y relojes;
  - archivos y directorios, con un caché de lectura porque cada lectura chica es una llamada IPC lenta en la Switch;
  - `epoll` mínimo.
- **dlopen:** `horizon/nxdl.c` emula `dlopen`/`dlsym` con `eglGetProcAddress` para el cargador de OpenGL.
- **Cambios en Ikemen (`patches/ikemen`):**
  - plataforma `nx`;
  - renderer GLES 3.2 adaptado a Mesa/nouveau: los sprites chicos y las paletas se agrupan en páginas de atlas, porque el driver falla pasadas unas 3000 texturas;
  - carga diferida de texturas;
  - límite de memoria del recolector de basura (80% del bloque del heap);
  - diagnóstico de cuelgues.
- **go-sdl2:** lleva un parche mínimo para enlazar SDL2 estático de devkitPro.

## Limitaciones conocidas

- El heap de Go está limitado a 1 GiB (el resto queda para Mesa/GPU).
- No se probaron el juego en línea ni los escenarios 3D (`.glb`).
- El emulador Eden sirve para probar la lógica, pero dibuja mal o lento; lo visual hay que probarlo en la consola.

## Licencias y créditos

- El código propio de este repositorio (`horizon/`, `app/`, `nxconfig/`, scripts y parches) es **MIT**: ver [LICENSE](LICENSE).
- **El `.nro` publicado incluye componentes GPL** (FFmpeg de devkitPro, enlazado estático con `libpostproc`), así que el binario se distribuye bajo los términos de la GPL. El código fuente correspondiente es este repositorio más los proyectos indicados en [THIRD_PARTY.md](THIRD_PARTY.md).
- Ikemen GO: MIT, © Ikemen GO contributors. Go: BSD-3-Clause. libnx: ISC. SDL2: zlib. Mesa: MIT. libxmp: MIT.
- Logo de Ikemen GO (icono del `.nro`): **Cylia Margatroid**, CC BY 3.0. Los recursos del screenpack incluidos en el paquete de la SD llevan sus licencias en `switch/ikemen/LICENSES.txt`.

Este proyecto no está afiliado a Nintendo ni a los autores de Ikemen GO. No incluye juegos ni personajes de terceros.
