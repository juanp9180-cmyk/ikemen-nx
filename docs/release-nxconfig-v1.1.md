**Beta: not tested on hardware yet.** Ikemen NX Config v1.1 adds editors for `select.def` and the screenpack's `system.def`. The engine (`ikemen.nro`) is unchanged from v0.3.0.

### New in Ikemen NX Config v1.1

- **Roster** (`select.def`):
  - turn characters on/off (the cell stays empty, so the select grid doesn't shift);
  - toggle members of shared cells (`slot = { ... }`);
  - move characters between cells (Y);
  - search by name (X, on-screen keyboard);
  - add characters found in `chars/` that aren't listed yet (−).
- **Stages & music:** turn stages on/off, pick each stage's music from the files in `sound/`, and add stages found in `stages/`.
- **Screens** (`system.def`):
  - VS screen time and fade, victory and continue screens, demo mode;
  - select grid rows and columns;
  - show, hide or rename the title menu items.
- **File editor:** line-by-line editor for `config.ini`, `select.def`, `system.def` and the screenpack's other `.def` files, using the Switch keyboard.

Disabled entries are kept as comments (`;nxoff:` / `;nxhide:`) so they can be turned back on. The first save of each file still writes a `.bak` copy.

### Install

Replace `switch/nxconfig/nxconfig.nro` on your SD card with the one attached here. Back up your `select.def` and `system.def` first, since this is a beta.

Please report problems with a photo of the screen and, if possible, the `.bak` and edited files.
