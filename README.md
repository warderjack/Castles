# Castles: Siege & Conquest — native Linux port (work in progress)

Runs the original classic-Mac game on Linux. The game's 68k code has been mechanically
translated to C (tools/lift.py -> port/gen/lift_*.c, verified against the Musashi interpreter),
and the Mac Toolbox (QuickDraw, windows, menus, dialogs, sound,
QuickTime/Cinepak movies, file manager) natively on SDL2. No Apple ROM or system software is used.

You must supply your own copy of the game (the folder containing
`Castles - Siege & Conquest.bin` plus "Castles Data", "Castles Movies", "Castle Plans").

## Build (Linux Mint / Ubuntu)
    sudo apt install build-essential git libsdl2-dev
    cd port && ./build.sh
## Run
    port/build/castles_host "/path/to/Castles - Siege and Conquest"
### Standalone build (recommended)
No emulator, just gcc + SDL2:

    cd port && ./build_native.sh
    build/castles_native <rsrc.fork> "/path/to/game folder"

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
