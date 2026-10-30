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

## Lifter (68k -> C) status
`tools/lift.py` translates all 907 routines of the CODE segments into `port/gen/lift_seg*.c`. The interpreter hands off to translated code at routine entry; anything unsure bails back (`LBAIL`).
`LIFTCHECK=N` (see `port/lc.sh`) runs each translated routine in a dry run, rolls back, replays it in Musashi and compares registers, flags and RAM. Current result on three scenarios (new game/visit, menus, load castle): 0 mismatches, ~2000 routines verified each.
Gotcha fixed: relocation constants use the fixed STRS pool base (A4 at startup), not the live A4 (which game code reuses as a scratch register).

Update: `LIFTCHECK` now also verifies partial runs up to the first trap or bail (previously skipped). That exposed a generator bug: routines whose entry is not their lowest address (shared tail code) must `goto` the entry first. Fixed in `tools/lift.py`. With lifting on by default the game plays the same and executes roughly 2.7x more work per wall-clock second than the pure interpreter (`NOLIFT=1` disables it; `LIFTMAX=N` caps lifted calls for bisecting; `LIFTLOG=1` logs entries and bails).

Update: analysis now also seeds routines reached only by pointer (pea/lea pc-relative targets and unreferenced `link a6` words), covering ~95% of CODE bytes. Segment 7 relocations were added. Translated routines can be resumed mid-body (after any trap or call) via per-function entry switches (`lift_mids`), so the interpreter no longer has to finish a routine after a bail. `NOMID=1` disables this. Checks: 4924 routines verified, 0 mismatches (load-castle scenario).

Experimental: `CASTLES_DET=1` (virtual clock driven by trap count, `DET_EVERY`/`DET_STOP`/`DET_YMAX` for RAM-hash checkpoints) was meant to allow trap-for-trap A/B runs of lifted vs interpreted code. It is not reliable yet (the game derails shortly after the intro), so it is off by default. End-to-end confidence currently comes from `LIFTCHECK` (local equivalence) plus scripted play-throughs (menus, castle load/save, Calais visit) with lifting on, which show no alerts or unimplemented traps.

## Test nondeterminism (important when bisecting)
The game's own Random wrapper (seg 2 +0x45d0) mixes low-memory Ticks (0x16C) into its seed, so new-game
setup (e.g. which castle is yours, hence "Visit" vs "Scout" on clicking Calais) depends on timing. Lifted and
interpreted runs differ in speed, so screenshot-pixel tests of game state are NOT deterministic across
configurations (shifting a click script by 40 ticks flips the outcome even with NOLIFT=1). Use LIFTCHECK
(state-equivalence) rather than screenshots to judge the lifter. Mid-routine entries were wrongly suspected of a
regression; they verify clean.
Debug env vars added: CHKPC=lo:hi (hex; force equivalence check at those entry/mid pcs), PCWATCH=lo:hi,
RNDLOG=1 (log Random calls), DUMPRAM=file (dump A5 world at exit).

## Lifter coverage round 2
- A5-world dispatch thunks (`bra.s` ladder -> `jsr d16(a5); jmp (a0)`) are executed natively in `lift_enter`.
- `gen/entries_extra.txt` = runtime-observed unlifted entry points ("seg:hexoff"), merged into the analyzer; refresh with
  `scripts/entry_iter.sh [script]` (runs a scenario with ENTLOG, merges, regenerates, rebuilds).
- Music driver: the MDRV resource is stream-cipher encrypted (seg2 +0xc22/+0xc56) then LZSS-packed (seg2 +0x1df4; 12-bit
  window, length = top nibble + 3, flag bit 1 = literal). `tools/mdrv.py` reimplements both; the result (29256 bytes, code in
  the first 0x4200) is analysed/lifted as pseudo-segment 14. The host registers it when lifted code first enters a block whose
  first 16 bytes match the decoded signature.
- Lifter gained addx/subx, roxl/roxr, andi/ori/eori to CCR and SR, move to/from SR.
- Livelock guard: a mid entry that bails straight back to its own pc is disabled.
- Result (c1 scenario, 100 s): interpreted instructions 55M -> 14M, lifted calls 132M -> 168M; LIFTCHECK 0 mismatches on c1/mn/t3.
- Return handling no longer uses a per-function R0: lifted calls keep a shadow stack of expected return addresses
  (`lift_rs`/`lift_rsp`); `rts` returns to the C caller only if it matches the shadow top, otherwise bails; top-level entries
  return to whatever address `rts` popped (`lift_ret_pc`). This makes mid-function fragments and Pascal glue (`jmp (a1)` returns)
  work without bailing. Bails in the c1 scenario: 1.45M -> 7k; interpreted instructions 55M -> 11M.
- `top bail targets` is printed at exit (where the interpreter still takes over).
- Jump tables (`lea T(pc),a1; adda.w 0(a1,d0.w*2),a1; jmp (a1)` guarded by `cmpi.w #N,d0`) are resolved by the analyzer
  (`jump_table()`), and lifted as a switch on the target offset.
- Mid-entry resume no longer requires a link frame (return handling is shadow-stack based).
- Backward branches call `LLOOP()`; every 3000 back-edges `lift_poll()` syncs state and runs the same housekeeping as the
  main loop (ticks, VBL/Time Manager tasks, SDL events, presenting). Without this, lifted busy-wait loops never yield.
  `NOPOLL=1` disables it, `MIDLINK=1` restores the old link-only mid entries (debugging).
- `lift_jb` is saved/restored around nested `lift_try` calls (tasks running guest code inside lifted code).

## Lifter coverage round 3
- Mid table now prefers haslink entries when several functions share a label (e.g. seg3 +0x223a).
- Livelock guard counts nested lift entries as progress; the old guard disabled legit loop mids (05a74e etc.).
- c1 100 s: interp insns 1.3M vs 85M lifted calls, 8k bails (top: 05a74e, 05a774 = trap yields). Remaining unlifted: 053688, c0xxxx (runtime-generated code).
- Blocking traps (rewound to YIELD_PC, e.g. Delay, MenuSelect, ModalDialog tracking) now wait in C inside `lift_trap` (poll + re-run hle) instead of bailing to the interpreter. `NOBLOCK=1` disables. Remaining redirects: ModalDialog guest filter procs (GCONT).
- Guest filter/user-item procs requested by dialog handlers (GCONT_PC) are now run as nested lifted calls inside `lift_trap` (`GCLOG=1` traces). Must `lift_sync_in()` before `lift_poll` there (poll's sync_out would clobber core SP) and save/restore the `gc` struct around the nested call. No trap-redirect bails remain in c1/mn/t3.
- SMOD resource 0 (456 B, sound module, loaded into heap, entered through its handle) is lifted as pseudo-segment 15 (registered by 16-byte signature `lift_smod_sig`, like MDRV/seg 14). Removed the last big interpreter loop (05373e, ~300k iters/100 s).
- `lift_special(pc)`: ROM trap stubs (STUB_BASE A-line), heap dispatch thunks (`bsr.s; dc.l tgt; movea (sp)+,a1; movea (a1),a1; jmp (a1)`), unloaded-segment `_LoadSeg` stubs and A5 jmp/thunk stubs are entered natively (hook + `lift_enter`); LoadSeg tail-enters the loaded entry. c1/mn/t3 now interpret only ~100 insns per 100 s (a handful of seg1 data stubs at 050e14, 063088, etc.). Remaining Musashi use = bail/resume plumbing (`lift_try` mid entries, ~3k bails/100 s) and `call68k` shell.
- `NOINTERP=1` logs every instruction the interpreter would still execute (non-A-line, not a yield/sentinel pc); `=2` quits at the first. c1/mn/t3 now show only the end-of-run stop pc. The game's own patched A9F0 wrapper (`jsr loader; jmp ROM-stub` glue at 050e14, in the app's data block) is entered via `lift_special`; a ROM-stub trap called from the glue must skip the `patched[]` check (`lift_stubcall`), otherwise it recurses forever. LoadSeg tail-enters the entry, so don't overwrite `lift_ret_pc` after it.
