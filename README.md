# Castles: Siege & Conquest — native Linux port (work in progress)

Runs the original classic-Mac game on Linux. The game's 68k code has been mechanically
translated to C (tools/lift.py -> port/gen/lift_*.c, verified against the Musashi interpreter),
and the Mac Toolbox (QuickDraw, windows, menus, dialogs, sound,
QuickTime/Cinepak movies, file manager) is reimplemented the Mac Toolbox (QuickDraw, windows, menus, dialogs, sound,
QuickTime/Cinepak movies, file manager) natively on SDL2. No Apple ROM or system software is used.

You must supply your own copy of the game (the folder containing
`Castles - Siege & Conquest.bin` plus "Castles Data", "Castles Movies", "Castle Plans").

## Build (Linux Mint / Ubuntu)
    sudo apt install build-essential git libsdl2-dev
    cd port && ./build.sh
## Run
    port/build/castles_host "/path/to/Castles - Siege and Conquest"
Standalone build (no emulator at all, gcc + SDL2 only): `cd port && ./build_native.sh && build/castles_native <rsrc.fork> "/path/to/game folder"`.
Saves and preferences are written into that folder (as MacBinary `.bin` files / plain files).
Headless testing: `CASTLES_HEADLESS=1 CASTLES_SCRIPT=script.txt` (see NOTES.md).

## Status
See NOTES.md. The 68k code is now fully translated and runs natively; the Musashi build is kept as the reference/fallback.
Not yet done: Open Game / Standard File dialogs are non-interactive. Where behaviour is uncertain the Mac version is the reference;
the DOS demo is only used for cross-checking game data and logic.
