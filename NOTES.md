# Castles: Siege & Conquest -> Linux  (work in progress)

## Approach
Run the original 68k code in the Musashi emulator and reimplement the Mac Toolbox
(high-level emulation, "HLE") natively. Convert segments to C later, using the emulated
build as the behavioural reference.

## What is known about the binary (all verified)
- Built with Symantec THINK C. 13 CODE segments (~200 KB), CODE 0 = jump table (3608 bytes).
- A5 world: 30002 bytes below A5, 3640 above, jump table at A5+32.
- Startup (CODE 1): loads 'STRS' as the A4 string pool; fills globals between CurStackBase and A5
  from DATA 0 + ZERO 0 (a zero word in DATA = "clear N bytes", N read from ZERO); relocates with DREL
  (bit0 of each entry: 0 = add A5, 1 = add A4).
- It patches _LoadSeg ($A9F0) and _ExitToShell ($A9F4) via Get/SetTrapAddress. The _LoadSeg wrapper
  applies per-segment CREL relocations on first load (flag = bit 15 of the segment's first word).
  The host therefore runs the game's own wrapper as real 68k code; the ROM _LoadSeg is HLE.
- Segment header: word0 = JT byte offset of first entry, word1 = entry count; routine address =
  segment base + 4 + stub offset. Loaded stub form: seg# (2) / 4EF9 (2) / address (4).
- Uses SANE (Pack4, $A9EB) for floating point -- must be implemented (host `long double` fits).
- Needs: File Manager (data in "Castles Data", "Castles Movies", "Castle Plans" next to the app),
  QuickDraw + Color QD, Window/Menu/Dialog/Control/Event managers, Sound Manager + own MIDI driver
  (Midi/SONG/INST/MDRV resources), QuickTime movies (Movie01..33), system fonts (none in the app).

## Host (port/)
- host.c: RAM image, resource-fork parser, handle heap, trap dispatcher (per-instruction hook),
  Memory/Resource/Segment managers, init traps. Unimplemented traps halt with a clear message.
- Trap keys: toolbox 0x400+(w&0x3FF), OS w&0xFF. Stubs at 0xF00000 let patches chain to "ROM".
- Run:  port/build.sh && port/build/castles_host work/castles/rsrc.fork

## Status
Boots through the THINK C runtime and into the game's init (105 traps). Next: File Manager HLE
(FSDispatch GetFCBInfo, PBHOpen, PBRead, ...), then QuickDraw/windows/events on SDL2.

## Environment notes
PyPI and apt are blocked in the cloud workspace; public GitHub clones work. Tools built from source:
Capstone (tools/capstone), SDL2 (tools/sdl2-install), python-rsrcfork (tools/rsrcfork).
Set LIBCAPSTONE_PATH=tools/capstone/build when using scripts/m68kdis.py.

## Status update
- Boots through init, SANE, title screen, intro movie (QuickTime Cinepak via `port/movie.inc`), New Game dialog, strategic map, menus (incl. hierarchical), pictures, dialogs/controls.
- Stubs: AppleEvents (A816), FindFolder (A823), FSMakeFSSpec (AA52), TrackBox, PtToAngle, Debugger.
- TODO: real audio, Standard File (Pack3), convert CODE segments to C.
