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

## Standalone build (no Musashi)
`port/build_native.sh` -> `build/castles_native`: gcc + SDL2 only. `port/native/m68k.h` is a 19-word register file plus `m68k_execute()` that drives `castles_instr_hook()`; every guest instruction runs as lifted C (gen/lift_*.c), traps go through the same HLE. If the game ever reaches code that was not lifted it prints `[native] cannot execute pc=...` and stops; rebuild with `./build.sh` (Musashi) and `NOINTERP=1` to find the pc, then add it to `gen/entries_extra.txt` and re-run `tools/lift.py`. LIFTCHECK / interpreter fallback exist only in the Musashi build, which stays the reference. c1/mn/t3 produce the same screens and run ~5% more lifted calls per 100 s in native.
- `scripts/smoke.sh [binary]` runs every `scripts/play/*.txt` input script (menus, New Game, scout, visit/design mode, random monkey runs) headless and fails on `[native] cannot execute`, unimplemented traps, tasks that don't return, or crashes. All 7 pass on `castles_native`.
- Design mode verified: Visit (Admin menu) -> Territory > Load Castle > Castle Plans > Contra -> "Click and drag to place" -> plan placed (59 points), scroll by drag (`scripts/play/design.txt`). Territory > Build Mode stays disabled at 0% completion with starting resources; unverified whether that is the original rule (it may need resources/points). Removed old `[probe]` debug lines from host.c.
- Build Mode enable rule (from the game's own code, 26b086 menu updater -> helper 26cece): the Territory "Build Mode" item (`blmd`) is enabled by `d6` = number of selected design pieces (field +$6e of each list node); Cut/Copy likewise. So it stays greyed until pieces are selected in Design mode; this is game logic, not an HLE gap. `MENUEN=1` logs every Enable/DisableItem with caller. Open: select a piece by clicking in Design mode and confirm Build Mode then enables (scripted clicks are timing-sensitive, the Calais visit sometimes doesn't start with fixed tick times).
- Resolved: Build Mode enable = game helper at jump-table +0x2ca (bb966) which scans the player's 6 order slots for an active order of kind 5 (the "Build Castle in <county>" order). So Build Mode is gated on having issued that Admin order (itself gated on resources), exactly as in the original; selecting pieces only affects Cut/Copy. `scripts/play/design2.txt` is the timing-robust Design-mode script (dismisses the opening dialog first, selects a tower: dotted outline, Cut/Copy enable).
- FIXED the "dark remnant" after closing dialogs (Scout/Recruit): ShowHide (A908), HiliteWindow (A91C), MoveWindow (A91B) and CheckItem (A945) tested their Pascal Boolean with `rd16(sp) != 0`. A Boolean is the HIGH byte of the pushed word (low byte can be garbage), so `ShowHide(w, FALSE)` from the game left the window visible. Use `rd8(sp)`. `WMT=1` now also logs A900-A90F (ShowHide etc.). When adding handlers: Booleans in = `rd8(sp)`, Booleans out = `wr8(sp, v ? 1 : 0)` (byte at the lower address).
- `CASTLES_TICKSCALE=N` runs the Mac tick clock N times faster (headless soak tests); script `T` times are in scaled ticks. `scripts/play/long_monkey.txt` (60000 ticks of random clicks/menus) is meant for `CASTLES_TICKSCALE=6`; it passed with no unlifted code or crashes (events like the trivia dialogs get exercised). Battles not yet triggered: needs enemy contact, which in a short run depends on random events.
- `scripts/play/long_soak.txt` (`CASTLES_TICKSCALE=12`, ~10 min): recruits repeatedly and dismisses events; reached April 1313 (troop payments, recruit completed, scouting) with 1.8G lifted calls and no failures on `castles_native`. Both long scripts are opt-in (`LONG=1 scripts/smoke.sh`, with TICKSCALE exported).
- Rare (1 in ~7 long runs, Burgundy soak) crash seen once: a VBL/TM task thunk at 13629c jumped to 0x00fffe (stale/overwritten thunk target). Not reproduced with TASKLOG=1 (logs each host-called routine with its bytes). The native shell now abandons a host-called routine that lands in unlifted code (`[native] abandoning host-called routine`) instead of quitting; failures from game code itself still stop with `cannot execute` plus the last 32 lift entries and stack. Suspect: Time Manager records (RmvTime only clears the wake time; InsTime appends duplicates) outliving their memory.
- Time Manager: `InsTime` no longer duplicates entries, `RmvTime` removes the record, `PrimeTime` on an unknown record registers it; the task pointer is captured before the callback. 3 parallel Burgundy year-long soaks (~1G lifted calls each) ran clean afterwards.
- Perf: every `getenv("...")` is now resolved once per call site (macro in host.c), and the per-trap recent-trap ring stores raw fields instead of snprintf'ing a string per trap. Throughput in the c1 scenario went from ~1.8M to ~3.8M lifted calls/s (trap rate 0.35M -> 1.2M/s). The game still uses ~90% of one core when running flat out (it redraws continuously, like it did on real hardware); `idle_hint` sleeps (1 ms) when the game polls events and finds none. SDL with `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy` runs the non-headless path cleanly.

## Idle CPU
WaitNextEvent (A860) sets `idle_hint` after 2 consecutive null results (or when `sleep` != 0), so `lift_trap` sleeps ~1 ms. Standalone build CPU: ~8 s user per 60 s wall (was ~53 s). All 9 smoke scripts pass unchanged.

## Routine coverage
Every translated routine starts with `LCOV(off>>1)`; `COVLOG=file` appends the `seg:off` of each routine that ran. `python3 scripts/coverage.py file [-v]` summarises. All scripts in scripts/play (incl. long_monkey, long_soak) together reach 702 of 1334 routines (53%): seg 5 (1/80), 11 (53/182), 2 (86/211), 13 (43/100) are mostly unreached, presumably battles/sieges and other late-game logic. Regenerating with the LCOV change kept LIFTCHECK at 0 mismatches (c1 5394, mn 4878, t3 4581 routines).

## Attack attempt (battles)
Military > Attack is enabled only for a selected non-owned county once a recruit order has completed (and no modal dialog is open). `scripts/play/attack_try.txt` (not part of smoke.sh) recruits infantry, spams answer/OK clicks to clear random trivia dialogs, selects Chartres, cancels the Scout dialog the extra click opens, then opens Military > Attack and OKs the dialog with Military 3: the order "Attack Chartres" is queued and progresses. A blind script cannot reliably wait for the attack to resolve (random event dialogs, order replaced), so seg 5 (80 routines, likely battle/AI computation, ~no traps) is still unexercised. Needs interactive play or a smarter script with screen checks.

## First real-desktop run (user report)
Opening sound had clicks/pops: audio was fed only at real-time pace (refill after each buffer's duration), so any stall underran. `snd_tick` now keeps ~300 ms queued in SDL and refills immediately (headless / no-device keeps the old pacing). User also reported a crash after clicking at the title screen with no `[native]` message (likely a signal); not reproducible here under Xvfb (SDL x11, dummy audio) or valgrind headless. Ask for exit status (139 = segfault) and gdb backtrace.

### Crash fix (user's first run)
Fresh game folder (no Castles Prefs.bin) segfaulted right after New Game > OK: the game AddResource()s a prefs resource, then ReleaseResource/DetachResource'd it; `Res` had `buf == NULL` and its handle was gone, so `res_get_handle`/`res_bytes` read from NULL+off. Fix: `res_snapshot()` copies handle-backed resources into private storage on Release/Detach, plus NULL guards. Test with a copy of the game folder with `Castles Prefs*` removed (previous smoke runs always had a prefs file, which hid it). Backtrace came from `gdb bt` on the user's machine.

### Sound investigation (user: no sound on real desktop)
`SNDLOG=1` logs buffers queued to SDL (min/max), 'snd ' GetResource, SndPlay/SndDoCommand/SndDoImmediate. Findings: the game's own mixer (MDRV double buffer, 22254 Hz mono U8, 370-byte buffers) outputs only constant 0x80 then constant 0x00 across long_monkey/recruit runs -> no voice is ever mixed. The game calls GetResource on ~12 'snd ' ids (effects) but never SndPlay; 'IntroSong' (music) is missing from the distribution (-43). So effects either are triggered through SMOD/MDRV paths that never fire in my runs or something upstream (sound-capability checks, prefs, Gestalt) disables them. Next: find which routine hands a 'snd ' handle to SMOD/MDRV and why it isn't reached.

### Sound root cause
The game's MDRV buffer proc (seg 14 +0x6de) tests low-memory SdVolume (byte at $260); 0 -> it writes silence (constant 0x00 buffers). Host never initialised it. Now `wr8(0x260, 7)` at boot (Mac default); the mixer then produces real 8-bit U8 data centred on 128 (verified with SNDLOG: buffers min~112 max~144 during recruit scenario). Smoke suite still passes. Not yet heard on real hardware; audio path: SDL_QueueAudio, 22254 Hz mono U8, ~300 ms queued ahead.

### Verified on a real Linux Mint desktop (user)
Window + mouse + New Game work (after the Prefs-resource fix); attack on Reims (as Valois, 1 infantry recruited) reached a battle with a correct-looking map; Save Game (silent overwrite after first save) and Save/Open round-trip verified. Sound not yet re-tested after the SdVolume fix (needs pull + rebuild). Music: 'IntroSong' file absent from the distribution (-43).
Sound confirmed by user on real hardware after the SdVolume fix: in-game music plays (so the missing 'IntroSong' only affects the intro track).

### Memory fixes (user's long session crashed on reload)
User log: `[heap] out of memory (n=145204 free=322016 max=75728)` then `[native] cannot execute pc=c0800c` (game used a NULL handle after a failed NewHandle), plus screen artifacts earlier. Causes found:
1. **Framebuffer inside the heap range.** FB_BASE=0xA00000 (640x480) lay inside HEAP_START..HEAP_END, so a nearly full heap handed out blocks over the screen buffer (artifacts). `heap_init` now excludes [0xA00000,0xA50000).
2. **DisposeGWorld (AB1D selector 4) was a no-op**; each Open/Close Game cycle leaked ~0.8 MB (37 live 189800-byte offscreen pixel blocks after 6 cycles). Now frees pixels, pixmap, device and port. `scripts/play/openclose_try.txt` (6 save/close/open cycles; run with `HEAPLOG=1`): heap free at exit 3.6 MB -> 9.2 MB. Not in smoke.sh (needs the file the script itself saves).
3. **No purging.** Resource handles flagged purgeable are now freed on demand when an allocation fails (`purge_for`, reload through GetResource/LoadResource); HPurge/HNoPurge set/clear the purge bit. Only file-backed, unlocked, unmodified resources are purged.
`HEAPLOG=1` prints live-allocation groups (count x size, trap return address) and heap stats at exit; useful for finding further leaks. Still no-ops: DisposeRgn, DisposPixMap, DisposeCTable (small).

### Sound effects (open)
Music works (SoundMusicSys-style engine: 'SONG' resources + mixer MDRV). Seg 13 holds the sound wrappers: +0x87c plays a 'SONG' (gated by prefs byte $1a = Music), +0xad4/+0x90a stop it (cmd 2), +0x690 services both Music ($1a) and Sound Effects ($1b) flags; the dispatcher is seg 2 +0x14ae (JT A5+0x792; cmd 3 = idle poll ~112k calls per soak). Callers of the seg-13 sound routines: seg 5 +0x4a2c (7+7 calls), seg 6 +0x5b46 (13 calls) (battle code), seg 3 +0xe46/+0x2342/+0x2366/+0x255e, seg 7 +0x1618/+0x1ba2, seg 10 +0x746. No effect command is ever issued by my scripts (only startup cmds 0/1/2 and idle cmd 3). `SNDCMD=1` logs every dispatcher call (cmd, word, long). Plan: have the user run a real battle with `SNDCMD=1 SNDLOG=1` and send the non-idle lines.
