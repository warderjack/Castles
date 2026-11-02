# CD release (experimental)

Work on the MacPlay "Castles: Siege & Conquest Enhanced CD-ROM" (v1.1). Kept separate from the floppy-release port:
nothing here is used by `install.sh` or by `port/build_native.sh`, and the floppy translation (`port/gen/`) is untouched.

- `build_host.sh` builds `cd/build/host_cd`, the interpreter-based host (Musashi), from the shared `port/` sources.
- `run.sh` runs the CD program in it, headless. Needs the CD program's resource fork and a game folder:
  `CDRSRC=".../Castles CD.rsrc" CDGAME=/path/to/folder cd/run.sh [slices]` (folder holds `Castles CD Data`, `Castles CD Movies`).
- `dis.py START END [segment]` disassembles part of a CODE resource extracted into `cd/extracted/` (not published: it is the game's code).

Everything the CD release needed from the shared host is additive and does not change what the floppy build does
(new trap handlers it never calls, plus one start-up step guarded by the CD-only resource 'CS&C'). See NOTES.md, "CD release".
