# CD release (experimental)

Work on the MacPlay "Castles: Siege & Conquest Enhanced CD-ROM" (v1.1). Kept separate from the floppy-release port:
nothing here is used by `install.sh` or by `port/build_native.sh`, and the floppy translation (`port/gen/`) is untouched.

- `build_host.sh` builds `cd/build/host_cd`, the interpreter-based host (Musashi), from the shared `port/` sources.
- `run.sh` runs the CD program in it, headless. Needs the CD program's resource fork and a game folder:
  `CDRSRC=".../Castles CD.rsrc" CDGAME=/path/to/folder cd/run.sh [slices]` (folder holds `Castles CD Data`, `Castles CD Movies`).
- `dis.py START END [segment]` disassembles part of a CODE resource extracted into `cd/extracted/` (not published: it is the game's code).

Everything the CD release needed from the shared host is additive and does not change what the floppy build does
(new trap handlers it never calls, plus one start-up step guarded by the CD-only resource 'CS&C'). See NOTES.md, "CD release".

## Installing the CD release (experimental)
`./install.sh` first (builds the shared parts), then `./cd/install_cd.sh /path/to/CD.cue` (.iso/.bin/.zip also work).
It extracts the game to `~/Castles-game/CD/Castles` and adds the command `castles-cd`. The floppy install is untouched.
Start it with `castles-cd` (or `./cd/run_cd.sh`).
Status: plays the game from the title screen through New Game and the strategic map. Working: intro movie (with sound), Encyclopedia, Castles and Tutorial movies (Cinepak and Animation video, 8/16-bit sound), About Castles, in-game music from the game files. Not done: the CD's own audio tracks, translation of the CD's 68k code to C (the interpreter is slower than the floppy build).

Diagnostics (environment variables): `CASTLES_PERF=1` prints one timing line per second to stderr; `QTDBG=1` logs QuickTime calls; `NOMOVSND=1` turns movie sound off; `MOVSNDFMT=fmt,ch` forces the movie sound format (0=u8, 1=s16, 2=s8; 1 or 2 channels).
