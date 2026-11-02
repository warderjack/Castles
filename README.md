# Castles: Siege & Conquest — native Linux port (work in progress)

Runs the original classic-Mac game on Linux. The game's 68k code has been mechanically
translated to C (tools/lift.py -> port/gen/lift_*.c, verified against the Musashi interpreter),
and the Mac Toolbox (QuickDraw, windows, menus, dialogs, sound,
QuickTime/Cinepak movies, file manager) natively on SDL2. No Apple ROM or system software is used.

## Game files (not included)
This project contains only the port, not the game. Castles: Siege & Conquest is still copyrighted
(Interplay), so its files cannot be distributed here. You need your own copy of the **Macintosh** version
(the **floppy-disk version**; see below) (the folder containing `Castles - Siege & Conquest.bin` plus "Castles Data", "Castles Movies", "Castle Plans").
Typical sources are your original Mac CD/disks, or a disk image or archive made from them (extract archives as
described in Quick start). This port is built for and tested with the **floppy-disk release** of the Mac game. A CD-ROM release also exists;
it is a different release that has not been tested here and may not work.
The Windows/DOS release of "Castles II" sold on some stores is a different program
and does not work with this port. No Apple ROM or system software is needed.

## Quick start (any Linux distribution)
    git clone https://github.com/warderjack/castles
    cd castles
    ./install.sh

`install.sh` checks for a C compiler and the SDL2 development package (and offers to install them with your
distribution's package manager: apt, dnf, pacman, zypper, apk, xbps or emerge), builds the game (about a minute),
looks for your game folder, and adds a `castles` command, an entry in your application menu (Games) and a double-clickable icon on your Desktop.
Then start it by double-clicking the icon (if your desktop shows a warning, right-click it and choose Allow Launching). Useful options: `--game DIR` (give the game folder),
`--yes` (no questions), `--no-deps` (never install packages), `--no-shortcut` (no Desktop icon), `--uninstall`.

### Giving the installer your game files
Your game folder is the one that contains `Castles Data`, `Castles Movies` and the game's `.bin` file.
Point the installer at it with `./install.sh --game "/path/to/it"`, or just run `./install.sh` and pick from the
list of folders and archives it finds. Archives are unpacked into `~/Castles-game/<archive name>/Castles` with `unar`
(the installer offers to install it), and the Mac resource forks are joined into the `.bin` files the game expects.
Three kinds of download are handled:

1. **A complete archive or folder** (.sit, .hqx, .cpt, .zip, .7z ...) holding the game, `Castles Data`, `Castles Movies`
   and `Castle Plans`. This is the simplest case and needs nothing special.
2. **A partial archive**, for example one that holds only the program and some data. The installer unpacks it and tells you
   exactly which pieces are missing. If you have several partial downloads, run the installer on each; it borrows
   `IntroSong` (the intro music) from another copy it knows about when one lacks it, and tells you which archive was missing it.
3. **A floppy-disk set**: the original game disks as Disk Copy 4.2 or raw images (`.image`, `.img`, `.dsk`), one file per disk.
   Put all the disk images in one folder and point `--game` at that folder. The installer reads the HFS disks itself,
   joins the pieces of the StuffIt installer stored on them, and unpacks the result.

Other disk images (`.dmg`, `.iso`, CD images) are not unpacked automatically; copy the files out of them first.
If the game starts with a "no game folder set" message, run `castles "/path/to/game folder"` once; it remembers the folder.

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
(real window and sound have been tested on Linux Mint, and the installer is tested in CI on seven distributions).
Saves and preferences are written into that folder (as MacBinary `.bin` files / plain files).
Headless testing: `CASTLES_HEADLESS=1 CASTLES_SCRIPT=script.txt` (see NOTES.md).

## Status
See NOTES.md. The 68k code is now fully translated and runs natively; the Musashi build is kept as the reference/fallback.
Open Game / Save dialogs (Standard File) are implemented natively; hiring, battles, the Black Market and movies have been played through and behave correctly. A rare sound-driver crash on very slow or loaded machines is documented in NOTES.md. Where behaviour is uncertain the Mac version is the reference;
the DOS demo is only used for cross-checking game data and logic.
