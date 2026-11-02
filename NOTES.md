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

## Issue queue (user-reported)
- Territory with a castle under construction: people/workers animate incredibly fast; looks tied to clock speed (TickCount/Time Manager pacing or per-frame loop not throttled). Investigate after sound.
- (Resolved/not reproducible) Load Game dialog button label: user later saw "Open" correctly in the current build.

- Verified by user on real desktop (2026-10-31): repeated castle placement across territories and close/reload cycles no longer crash or show artifacts (memory fixes 137a669).

## Sound effects root causes (fixed)
1. `Get1IndResource`/`GetIndResource` read their (type, index) args in the wrong order, so the game's effect-table builder (seg 13 +0x54c, called from the battle setup seg 7 +0x1618) found no 'snd ' ids 1000..9999 and registered zero effects; every effect request was silently dropped.
2. The MDRV driver selector stubs (`jmp 0(a0,d0.w)` table at SB14+0x34) were only lifted for selectors seen in earlier runs; selector 17 (play sample) and 20 were unlifted ("cannot execute pc=a80198"). All 27 stubs are now seeded in gen/entries_extra.txt.
Debug aids: SNDFX=<name> SNDFXAT=<ms> fires an effect from the harness (needs a running game); SNDDRV=1 logs driver selector calls; SNDVOICE=1 dumps effect voices.
- Verified by user (2026-10-31): sound effects audible in battle (infantry, archers) after aac0a15.

## Fast animation attempt (castle under construction)
The game always calls WaitNextEvent with sleep=0 (verified with WNESLEEP=1), so any frame-per-idle animation ran at host speed (~600 idle passes/s). WNE now paces null-event returns to CASTLES_WNEHZ (default 60/s; 0 disables; headless/deterministic mode unaffected). Awaiting user confirmation; if still fast, lower CASTLES_WNEHZ (e.g. 20) or locate the animation's own timer.
- Apple menu > About Castles: Siege and Conquest fails: shows a small Castles logo overlaid on the current screen; only occasionally the developer-credits image appears, usually nothing further happens (likely credits/animated about screen needing a timed sequence, window/redraw or event handling). Investigate.
- Menu bar highlight sticks: after opening one menu then another, previously opened titles (e.g. Edit, then Game, then Apple) stay highlighted; clears only after many menu clicks. Probably MenuSelect/HiliteMenu(0) not un-inverting the title on dismiss (invert-based, so double toggles).
- KNOWN FLAKE (headless smoke under CPU contention): fuzz/mn occasionally hit "[native] cannot execute pc=ad97bc op=6106" — the heap dispatch thunk used for the sound-driver proc (periodic entries ad97bc,c07c6c,a80120,a80164) gets overwritten/reused. Passes when run alone. Investigate stale thunk after free (low priority).

- FIXED (pending user check): sticky menu-title highlight. Dropdown open/close toggled the title with an XOR invert, which is not reversible through palette matching on the 8-bit screen; now the menu bar is redrawn with an explicit highlight id.

## WNE pacing rolled back to opt-in (CASTLES_WNEHZ=60)
With the 60 Hz null-event pacing on, headless smoke (openclose_try, fuzz, mn, attack_try, design2) crashes intermittently: after the game re-inits sound, the MDRV mixer (seg14 +0x20a2, called from the selector-4 service poll) writes through a pointer around the next heap objects (the 12-byte heap dispatch thunk at the driver proc and the new SndChannel get overwritten; thunk first/last words zeroed). Reproduces only when the game runs slower; with pacing off it never fails. Root cause not yet found; tools: HEAPWATCH=<hexaddr>, WRITEWATCH=lo,hi (lift writes, prints call chain + A0-A3), SNDDRV=1, QUAR=1 (quarantine of big frees: did not help).
Castle-build animation speed is therefore NOT fixed yet; need to find the animation's own timing instead of global pacing.
- Movies/animated sequences (intro movie, battle/event movies): user unsure whether they play or are triggered; needs a test pass (find movie triggers via PlotData/Intro PICT sequences, movie.inc, and which game events call them).
- Verified by user (2026-10-31): sticky menu highlight fixed (54f9769+).

## Sound-driver crash analysis (2026-10-31, still open)
- Reproduces ~60% of fuzz/mn runs with CASTLES_WNEHZ=30 (parallel headless); 0% with pacing off. Path: game start -> first SndPlayDoubleBuffer (chan a52e9c) -> IntroSong open fails (-43; file does not exist in the install) -> game shuts the driver down (selector zeroes driver words) and disposes the channel -> second SndPlayDoubleBuffer (chan ad97d0, same hdr/proc a807fe). Host poll then calls the driver mixer (seg14 +0x20a2, entered from proc a807fe) and it writes at chan-0x16..chan-0x12 (ad97ba/ad97be), i.e. into the game's heap thunk (ad97bc, 'bsr.s; dc.l target; movea (sp)+,a1; movea (a1),a1; jmp (a1)') allocated just before the new channel record. Because the game later calls through that thunk, the corrupted thunk makes "cannot execute pc=ad97bc".
- SndNewChannel HLE allocates only 96 bytes; a real SndChannel is ~1056 bytes (queue[128]); not yet shown to matter (writes are BELOW chan).
- Tried: SndDisposeChannel now stops the double-buffer poll (kept, harmless); stale-task guard for TM/VBL (reverted: false positives on a52e70); QUAR quarantine (no effect). Next idea: find which driver word supplies the base for the write (watch [a84378..]) and whether the game's second init should have run a driver init selector before the poll starts; or hold the first poll until the game has issued its init selectors.
- Castle-build animation speed still unfixed: idle loop is WNE/FindWindow/Button/AB1D per pass with sleep=0; EventAvail pacing (CASTLES_EAHZ=20 default) fixed only the credits scroll.

## Status 2026-10-31 (evening)
- Castle-building workers move very fast: user watched a YouTube playthrough of the original game; the builders move equally fast there, so this is faithful (original quirk), not a port bug. Closed; do not slow the idle loop (that would also avoid the sound-driver stale-pointer crash exposure). Credits scroll in the About box is still paced (CASTLES_EAHZ, default 20).
- IntroSong now exists (extracted from the 7-disk installer, which is one StuffIt archive: strip 100 bytes from each disk's data fork and concatenate; `unar` reads it). Implemented Sound Manager file play (SndStartFilePlay 0D000008, Pause 02010008, Stop 03020008, SndManagerStatus 03080008) plus stop on SndDisposeChannel / quiet+flush; played on a second SDL device. User verified: song plays and stops on click.
- The original install ships only 23 of 33 movie numbers (no 05,13,18,25,27-32): nothing missing from the user's copy.
- Text: glyphs are now 4bpp antialiased Liberation blended through the palette (gen/genfont.py; old 1-bit generator kept as gen/genfont_1bit.py.bak).
- tools/hfsx.py extracts classic HFS floppy images.

## Classic Mac fonts (optional, local) 2026-11-01
- The game uses QuickDraw system fonts: id 0 Chicago 12 (menus, copyright line), id 3 Geneva 9 (body/credits/dialog text), id 21 Helvetica 18 bold (map header). Not in the app; they live in the Mac System file (Apple's, not redistributable).
- Default text = Liberation Sans/Serif/Mono hard 1-bit strikes (gen/genfont.py, committed fontdata.h).
- Optional pixel-exact look: get the "classic Apple fonts" collection (tinkerdifferent.com resource 130: ChicagoFLF.ttf, geneva_9.ttf ...), then `python3 port/gen/genfont_local.py <fontdir>` writes port/fontdata_local.h (git-ignored, mixed font licenses so never commit) and rebuild. pick_strike() uses it for font ids 0 (Chicago 12) and 3 (Geneva 9; space advance forced to 4, bold = 1px smear). geneva_9.ttf: em 1024 = 16 px grid, render at 16 px.
- Debug: FONTLOG=1 prints each distinct (font id, size, face) the game draws.

## Movies (2026-11-01) — working
Root causes found in order: (1) `Gestalt('qtim')` was answered in the unused `hle_new.c`; the real handler is in `host.c` (now returns 0x02108000), which enables the Game > Movies/Double Size Movies items; (2) `FSMakeFSSpec` ignored the working-directory vRefNum (0x100+dirID) and only searched the current dir, so ":Castles Movies:" was never searched; now uses `resolve()` and the WD refnum; (3) the 'moov' resource holds the whole moov atom (skip its 8-byte header); (4) the game polls IsMovieDone/MoviesTask without pumping events, so each decoded frame is presented immediately. Verified by the user: the battle-start movie plays correctly. Debug: `QTDBG=1` (all QuickTime calls, OpenMovieFile, draw/clip), `QTSHOT=prefix` (mid-movie screenshot), `QTLOAD=1` (load-test all movies), `QTPLAY=NN` (self-test play). Open: Movie24 finishes after one MoviesTask poll (check), Double Size Movies untested, frame pacing untested.
- Automatic local fonts (2026-11-01): `port/build_native.sh` looks in `<repo>/fonts/` (git-ignored) for `ChicagoFLF.ttf` + `geneva_9.ttf` (or any .zip containing them, auto-unzipped), runs gen/genfont_local.py and rebuilds with them. Needs python3 + Pillow; falls back to bundled Liberation fonts with a note. To go back: delete `fonts/` and `port/fontdata_local.h`, rebuild.
- 2026-11-01: gray frame around the map = recorded PICT with PnSize(0,0) + FrameRect + RGBFgCol 0x7fff; zero-size pen must draw nothing (shape_op which==0 and PICT PnSize handler). Menu bar and window titles now plain Chicago (face 0). Chicago/Geneva 9 bundled (port/fontdata_classic.h, FONTS.md).

## Sound-driver crash, further findings (2026-11-01)
- Still only reproduces when the game runs slower (CASTLES_WNEHZ=30 fuzz, or heavy CPU contention in smoke: c1 flaked once). User sessions with pacing off are unaffected.
- SndNewChannel now allocates a full zeroed 1100-byte SndChannel (queue[128]); caller-supplied *chan is ignored on purpose (the game leaves stale pointers there, e.g. c03d66).
- At the second SndPlayDoubleBuffer the driver state (seg14 +0x41fe, a4 = proc+0x3b20) looks sane (voices 0+8). The corrupting write is `clr.l $3c0(a1)` at seg14 +0x1f06+..(1e2026) with A1=ad9bae, i.e. the voice pointer A1 is not inside the driver's voice array; the overwritten thunk is always chan-0x14 (the 12-byte thunk allocated just before the 3rd SndNewChannel's channel). Not caused by a freed 24K block (QUAR=1 gives the same pattern). Next idea: trace where A1 is loaded (loop at 1e1f6c: lea $22d2(a4),a1) -- a4 may differ from the value assumed above when entered via the selector-4 poll.

## Movie frame pacing (2026-11-01) — verified
Playback is chosen from wall-clock time against each movie's own timeline (media time scale 30, or 600 for Movie33), exactly how QuickTime's MoviesTask works, so speed does not depend on the host. `QTLOAD=1` now prints each movie's duration, average fps and frame-interval range: all 24 movies are ~10 fps (33/67/100 ms intervals, as authored), lengths 2.2-11.4 s. `QTPLAY=NN` (with a script that quits, e.g. `T 900 Q`) now reports frames presented and elapsed time: Movie01 37/37 frames in 3.64 s (length 3.63 s), Movie24 82/82 in 8.10 s (8.10 s), Movie33 22/22 in 2.24 s (2.24 s). The earlier "Movie24 finishes after one poll" worry did not reproduce. Still untested: the Double Size Movies option; and a slow host would drop frames (as QuickTime did) rather than slow down.

## Sound-driver crash — ROOT CAUSE FOUND AND FIXED (2026-11-01)
The mixer (seg14 +0x1f6c) walks its voices with a 16-bit do-while counter kept in driver memory (`$2952(a4)`). Its loop back-edges call the host poll (LLOOP), and when the game runs slowly that poll ran `snd_tick`, which called the mixer again *inside* the running mixer. The inner pass finished and left the shared counter at 0; the outer pass then did `subq.w #1` on 0, wrapped to 65535 and walked a1 up to 256 KB through the heap, clearing words (`clr.l $3c0(a1)`) over the heap dispatch thunk and the next SndChannel. That is why it only happened when slow (a poll lands in the loop), and why the damaged address was always "just before the newest channel". Fix: `snd_tick` is not re-entrant (`snd_in_tick` guard in tasks.inc). Verification: fuzz with CASTLES_WNEHZ=30 in six parallel runs crashed 5/6 before and 0/6 after (nested calls skipped 2-7 times per run); mn, openclose_try, attack_try, design2, c1 and recruit also clean under load. My first guess (voice count zero when the driver is shut down) was disproved by instrumentation and removed.

## CD release (MacPlay "Castles: Siege & Conquest Enhanced CD-ROM", Read Me v1.1) — first comparison (2026-11-01)
Disc: BIN/CUE, track 1 MODE1/2352 (Apple Partition Map + HFS), track 2 audio (starts 71:09:41). Layout: `Castles CD` (program), `Castles CD Data` (PlotData 596,577 B vs floppy 1,254,038; TaskData/TerrData/TerrData2 same sizes as floppy; ZoneData), `Castles CD Movies` (121 files: Movie0101..Movie1802 in sub-numbered series, Castle01-10, Game01-06; floppy has 23 Movie01..33), `Castle Plans` (11), `Castles Manual`, `MacPlay Demos`, installer, Register, Read Me. No IntroSong file.
Program: FAT binary. Data fork = PowerPC PEF (`Joy!peffpwpc`, 421,767 B, cfrg 'C2CWCD/PPC'); resource fork holds the 68k code in only three CODE resources: CODE 0 (24 B jump table), CODE 1 (210,966 B, main), CODE 3 (34,113 B) = 245 KB vs 205 KB in 13 segments on the floppy. 342 floppy resources vs 361 CD resources: 264 of the 307 common ones are byte-identical; new: 17 PICT (1300-1331), 4 MENU, DLOG/DITL 300/400/500/501, TMPL, CURS 135/136, STR# 5003/5004. vers "Castles CD 1.1, (c)1992-6". `SIZE` differs. A different build of the game: the floppy lift (port/gen/lift_seg*.c) does not apply; the 68k half would need its own lift run (single big segment, different entry/loader), plus new Toolbox traps it uses, CD-specific features (CD audio, many more/larger movies), and its data files.
Installer: reads the disc (hfsx handles raw CD sectors + APM; HFSX_LIST/HFSX_FILTER env), but the game-folder names differ (`Castles CD ...`), so it cannot be played yet.

### CD release: trial run in the interpreter host (2026-11-01)
Approach: run the CD program's own 68k code in the Musashi-based host (`cd/build_host.sh`, `cd/run.sh`) instead of lifting it first; the lifter comes after it works.
The program is a CodeWarrior-built C++ fat app: its 68k startup loads CODE 0, clears the A5 world, unpacks and relocates the compressed `DATA 0` (which also patches the absolute code addresses), patches LoadSeg/UnloadSeg/ExitToShell, and loads CODE 3 (Toolbox glue) itself. Code is 68020 (bsr.l etc.).
What was needed from the shared host to get it going (all additive; none is reachable from the floppy game): `HWPriv` (A098) no-op; AliasDispatch `ResolveAliasFile` (A823 sel 0x0C: nothing is an alias); Sound Manager `GetDefaultOutputVolume` (A800 sel 0x022C0018); FSDispatch `HOpenDF` (A260 sel 0x1A); a window call AAA3 sel 0x4D ignored; `GrayRgn` ($9EE) set to the screen below the menu bar only when the resource 'CS&C' exists. Debug aids: `ALERTDBG=1` (prints alert text), `PCLOG=file` (binary log of pc,d0 per interpreted instruction).
Result: it creates its Prefs file, starts the sound driver (SndPlayDoubleBuffer), builds the menus (File, Edit, Game, Encyclopedia, Castles, Tutorial) and reaches its title screen and main event loop (screenshot: castle, "Choose New Game from the File menu to begin", (c) 1992,1994,1995). Needs `Castles CD Movies` to exist or it shows "Unable to locate 'Castles CD Movies' folder".
Verification that the floppy build is unaffected: `port/gen/*` hash identical; floppy headless screenshots (title, recruit script) byte-identical between the previous published sources and the sources with the CD changes.
Not yet: New Game and play, movies (121 files, new names), CD audio track, lifting the CD code to C, installer producing a playable CD folder.

### CD: New Game crash root cause
SANE `x80_put` wrote a 12-byte extended (pad word at +10). CodeWarrior's CD build keeps true 10-byte extendeds on the stack, so the pad word clobbered the saved A6 of the caller's frame. Fixed additively: pad word is skipped when the CD marker resource ('CS&C') is present; floppy behavior unchanged. CD now reaches the strategic map (Valois, April 11, 1312) in the interpreter host.
CD gameplay check: after New Game, clicking a territory selects it (Reims) and the date advances; no unimplemented traps logged (cd/ng3.txt).

### CD: startup probe of Movie9901 (user report: "An internal error occurred")
The CD program probes for `Castles CD Movies/Movie9901` at startup. The cut-down sample lacked it, so it passed; the full disc has it, and the program then plays it through QuickTime. Needed (all additive, floppy path unchanged): resource forks stored as `name.rsrc` (open_fork), movies whose first `trak` is empty (use the first track with a sample table), QuickTime selectors GetMovieTime 0x39, GetMovieDuration 0x2B, LoadMovieIntoRam 0x07, PrerollMovie 0x06, GetMoviePreferredRate 0xF3, GetMovieColorTable 0x206, and OSEventAvail/GetOSEvent (A030/A031). Known: CD movie frames decode with noise (codec variant to check).

### CD: Cinepak codebook inheritance
Multi-strip Cinepak frames: strips after the first start from the previous strip's codebooks (unless frame flag bit 0 is set); strip 0 keeps its own from the previous frame. Codebooks are now per movie and per strip. This removed the noise/dark speckles in the CD movies and title picture.
Performance: the 68k path spends ~400M instructions in a bit-test loop (map/territory bitmap scan, CODE offsets near a80a42) on New Game and on each map click; native PPC code did this fast. Real fix: lift the CD's 68k code to C like the floppy build.

### CD: speed
Interpreter-only build: per-instruction hook has a fast path. The screen-dissolve masked blit (two tiny loops, ~60% of all interpreted instructions on New Game / map screens) is recognised by its instruction words and run natively (`cd_hle_mask` in host.c; `NOHLE=1` turns it off; screenshots are pixel-identical with and without). Remaining profile is flat (no bucket above ~13%); the real fix would be lifting the CD's 68k code to C.

### CD: speed and movies, round 2 (user-tested)
Slowness was not CPU: a per-second `CASTLES_PERF=1` log from the user's machine showed ~500 ms of every second inside `SDL_RenderPresent` (vsync) because two callers each presented ~30x/s, which also starved the emulation (30 slices/s vs 700+). Fix (CD host only, `NOLIFT` set): no vsync, one throttled `plat_present` (>=30 ms apart); idle `usleep` only when the recent slices were mostly idle event polls. Movies: QuickTime Animation ('rle ') video for the Castle01-10 stills, 'twos' sound (the intro declares 2-channel 8-bit but is 16-bit mono, found by ear with `MOVSNDFMT`), sound track queued to its own SDL device which is closed when a movie stops. Floppy check: `port/gen` unchanged vs 57a9c86; recruit-script screenshots identical to the pre-CD host for the first four checkpoints (later ones differ only by random in-game events/dates, which depend on timing).

### CD: disc audio track 2
User-checked: track 2 (just over 2 minutes) is the audio of the introduction. The game program has no reference to the CD audio driver, and the intro movie (Movie9901) already carries that sound (141 s, 16-bit mono 22050 Hz), which we play. So the disc track is redundant for the port and is not added to the installer. `cd/extract_audio.py disc.cue [outdir]` saves audio tracks as WAV if anyone wants them.

### CD: translated 68k code
`tools/lift_cd.py` (published as `cd/lifter/`) reuses `tools/lift.py`/`analyze.py` on CODE 1 (single CodeWarrior segment, 210,966 B, loaded at one guest address; resource byte 0 = SB[1]). 857 routines found (84% of bytes are code, the rest data); entries below 0x400 (start-up glue) are left to the interpreter (a mid-routine entry there mismatched in LIFTCHECK). Relocation: the program relocates itself at start-up; diffing RAM against the resource gave 326 longs: 275 A5-relative (`REL_A5+v`) and 51 code-relative (`SB[1]+v`, new reloc kind 'S1' in lift.py; floppy gen regenerates identically). `jsr $2c6.l` was the tell (an A5-relative jump-table address). The host runs the CD with the floppy glue unchanged; `cd_mode` (CD resource 'CS&C') selects CD-only behaviour: no vsync, throttled present, idle yield only when empty event polls come back to back (<600 us apart). Results: LIFTCHECK 0 mismatches / ~4000 routine runs; same scripted play-through 15.8 s CPU interpreted vs 8.4 s translated; idle title ~10% CPU either way.
