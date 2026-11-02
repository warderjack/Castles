# Castles: Siege & Conquest — native Linux port (work in progress)

Runs the original classic-Mac game on Linux. The game's 68k code has been mechanically
translated to C (tools/lift.py -> port/gen/lift_*.c, verified against the Musashi interpreter),
and the Mac Toolbox (QuickDraw, windows, menus, dialogs, sound,
QuickTime/Cinepak movies, file manager) natively on SDL2. No Apple ROM or system software is used.

You must supply your own copy of the game (the folder containing
`Castles - Siege & Conquest.bin` plus "Castles Data", "Castles Movies", "Castle Plans").

## Quick start (any Linux distribution)
    git clone https://github.com/warderjack/castles
    cd castles
    ./install.sh

`install.sh` checks for a C compiler and the SDL2 development package (and offers to install them with your
distribution's package manager: apt, dnf, pacman, zypper, apk, xbps or emerge), builds the game (about a minute),
looks for your game folder, and adds a `castles` command, an entry in your application menu (Games) and a double-clickable icon on your Desktop.
Then start it by double-clicking the icon (if your desktop shows a warning, right-click it and choose Allow Launching). Useful options: `--game DIR` (give the game folder),
`--yes` (no questions), `--no-deps` (never install packages), `--no-shortcut` (no Desktop icon), `--uninstall`.

Your game folder is the one that contains `Castles Data`, `Castles Movies` and the game's `.bin` file.
If your copy is an archive (.sit, .hqx, .zip, .dmg ...), extract it first, e.g. with `unar` (package "unar"
or "unarchiver"); keep the MacBinary `.bin` files as they are.

Manual build, if you prefer (needs gcc or clang, and SDL2 development files):

    cd port && ./build_native.sh
    build/castles_native "/path/to/game folder"

Package names for SDL2 development files: Debian/Ubuntu/Mint `libsdl2-dev`, Fedora `SDL2-devel`, Arch `sdl2`,
openSUSE `SDL2-devel`, Alpine `sdl2-dev`. The old Musashi-based reference build is `port/build.sh`
(`castles_host`); you do not need it.

Fonts: Chicago and Geneva 9 pixel fonts (public domain / CC BY 3.0) are bundled; see FONTS.md for credits and how to override them.

Expectations: it runs the lifted game code directly (no 68k interpreter). Idle CPU is low
(the game's event loop sleeps ~1 ms after two empty polls), roughly 15% of a core while the game is running.
If it ever reaches code that was not translated it prints `[native] cannot execute pc=...` and exits;
please report that line. Automated checks without a display: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy bash scripts/smoke.sh`
(real window and audio output have only been exercised through SDL's dummy drivers so far).
Saves and preferences are written into that folder (as MacBinary `.bin` files / plain files).
Headless testing: `CASTLES_HEADLESS=1 CASTLES_SCRIPT=script.txt` (see NOTES.md).

## Status
See NOTES.md. The 68k code is now fully translated and runs natively; the Musashi build is kept as the reference/fallback.
Open Game / Save dialogs (Standard File) are implemented natively; Build Mode and battles are not yet verified end to end. Where behaviour is uncertain the Mac version is the reference;
the DOS demo is only used for cross-checking game data and logic.
