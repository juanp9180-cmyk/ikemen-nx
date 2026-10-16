Primera versión pública de **Ikemen NX**, el port nativo de Ikemen GO v1.0.0 para Nintendo Switch (homebrew, Atmosphère).

### Instalación

1. Descomprime `ikemen-nx-v0.3.0-sd.zip` en la raíz de la microSD.
2. Si descomprimiste en Windows, quita el bit de archivo de las carpetas (Hekate → *Fix Archive Bit*, o `attrib -A -R /S /D E:\switch\ikemen\*`).
3. Abre el menú de homebrew manteniendo **R** al iniciar un juego (modo título) y elige **Ikemen GO**.

### Incluye

- `switch/ikemen/ikemen.nro` con el screenpack, los personajes y los escenarios de ejemplo de Ikemen GO v1.0.0.
- `switch/nxconfig/nxconfig.nro`, la app **Ikemen NX Config** para cambiar las opciones del arcade, las reglas, el video y sonido, el rendimiento y la grilla de selección sin PC.

### Notas

- Probado en consola real con Atmosphère 1.12 y firmware 23.
- La música se reproduce desde la SD (`BGMRAMBuffer = 0`) para no agotar la memoria.
- Si algo falla, adjunta `switch/ikemen/go_stdout.log` al reportar el problema.

Licencias: código propio MIT. El `.nro` incluye componentes GPL (FFmpeg/libpostproc) y se distribuye bajo esos términos; el código fuente es este repositorio. Ver `THIRD_PARTY.md`.
