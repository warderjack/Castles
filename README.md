# Castles: Siege & Conquest — native Linux port (work in progress)

Runs the original classic-Mac game on Linux by executing its 68k code in the Musashi
emulator and reimplementing the Mac Toolbox (QuickDraw, windows, menus, dialogs, sound,
QuickTime/Cinepak movies, file manager) natively on SDL2. No Apple ROM or system software is used.

You must supply your own copy of the game (the folder containing
`Castles - Siege & Conquest.bin` plus "Castles Data", "Castles Movies", "Castle Plans").

## Build (Linux Mint / Ubuntu)
    sudo apt install build-essential git libsdl2-dev
    cd port && ./build.sh
## Run
    port/build/castles_host "/path/to/Castles - Siege and Conquest"
Saves and preferences are written into that folder (as MacBinary `.bin` files / plain files).
Headless testing: `CASTLES_HEADLESS=1 CASTLES_SCRIPT=script.txt` (see NOTES.md).

## Status
See NOTES.md. Not yet done: Open Game / Standard File dialogs are non-interactive,
conversion of the 68k code to C. Where behaviour is uncertain the Mac version is the reference;
the DOS demo is only used for cross-checking game data and logic.
