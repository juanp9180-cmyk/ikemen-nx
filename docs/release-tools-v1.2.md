**Beta: not tested on hardware yet.** Two companion apps for Ikemen NX. The engine (`ikemen.nro`) is unchanged from v0.3.0.

### Ikemen NX Browser v1.0 (new)

- Opens the Switch's own web browser on download sites (AndersonKenya1 preconfigured). Launch hbmenu in title mode (hold R).
- When you tap *Download*, the file is saved to `sdmc:/ikemen-import/` and you return to the app.
- More sites can be added in `sdmc:/ikemen-import/browser.ini`.
- **Unconfirmed:**
  - whether the system browser passes AK1's Cloudflare check;
  - files hosted elsewhere (Mediafire, AK1's Dropbox for files over 150 MB) must still be downloaded on a PC and copied to `sdmc:/ikemen-import/`.

### Ikemen NX Config v1.2

- **Import** zip/rar/7z from `sdmc:/ikemen-import/`. The app detects and installs:
  - characters, added to the roster;
  - stages, added to the stage list;
  - music;
  - screenpacks and lifebars, with an option to switch to them;
  - full MUGEN/Ikemen games, installed as a new `switch/<name>/` with the engine copied in and the known MUGEN-only `system.def` quirks fixed.
- **Create a new game:** a clean Ikemen GO base bundled in the app (default screenpack, Kung Fu Man) in `switch/<name>/`, shown with its own name in the homebrew menu.
- **Screenpack** and **Lifebars** selectors for each game.

### Install

Copy `nxconfig.nro` to `switch/nxconfig/` and `nxbrowser.nro` to `switch/nxbrowser/`. Back up your `select.def` and `system.def` first, since this is a beta.
