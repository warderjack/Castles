/* Returns via W(PC) -- handler may redirect; otherwise PC=ret. */
#include <time.h>
static uint32_t ticks_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)((t.tv_sec * 1000ull + t.tv_nsec / 1000000) * 60 / 1000); }
static int16_t res_err = 0;
#define FB_BASE 0x00A00000u
static uint32_t cur_a5(void) { return R(M68K_REG_A5); }

static uint32_t set_handle_size(uint32_t h, uint32_t n) {
    uint32_t s = slot_of(h);
    if (n > hsize[s]) { uint32_t nb = alloc_block(n); memcpy(ram + nb, ram + rd32(h), hsize[s]); wr32(h, nb); }
    hsize[s] = n; return 0;
}
static void init_graf(uint32_t P) {
    wr32(0x824, P);
    for (int i = 0; i < 8; i++) { wr8(P - 8 + i, 0x00); wr8(P - 16 + i, 0xFF); }
    static const uint8_t g[8] = {0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55}, lg[8] = {0x88,0x22,0x88,0x22,0x88,0x22,0x88,0x22}, dg[8] = {0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD};
    for (int i = 0; i < 8; i++) { wr8(P - 24 + i, g[i]); wr8(P - 32 + i, lg[i]); wr8(P - 40 + i, dg[i]); }
    wr32(P - 122, FB_BASE); wr16(P - 118, 640); wr16(P - 116, 0); wr16(P - 114, 0); wr16(P - 112, 480); wr16(P - 110, 640);
    wr32(P - 126, 1);       /* randSeed */
    wr32(P, 0);             /* thePort */
}
static void hle(uint16_t w, uint32_t ret) {
    ntraps++;
    int redirected = 0;
    uint16_t n = (w & 0x800) ? (w & 0xFBFF) : (w & 0xF8FF);   /* normalised trap word */
    if (trace_limit > 0) {
        trace_limit--;
        fprintf(stderr, "[trap %04X %-18s] D0=%08x A0=%08x A1=%08x SP=%06x ret=%06x\n", w, tname(w), D(0), A(0), A(1), SP, ret);
    }
    uint32_t sp = SP;
    switch (n) {
    /* ---- Memory Manager ---- */
    case 0xA01E: SETA(0, alloc_block(D(0))); SETD(0, 0); break;
    case 0xA022: SETA(0, new_handle(D(0))); SETD(0, 0); break;
    case 0xA066: SETA(0, new_handle(0)); SETD(0, 0); break;        /* NewEmptyHandle */
    case 0xA01F: case 0xA023: SETD(0, 0); break;                   /* Dispose* */
    case 0xA020: SETD(0, 0); break;                                /* SetPtrSize (no-op) */
    case 0xA021: SETD(0, rd32(A(0) - 4)); break;                   /* GetPtrSize */
    case 0xA024: SETD(0, set_handle_size(A(0), D(0))); break;      /* SetHandleSize */
    case 0xA025: SETD(0, hsize[slot_of(A(0))]); break;             /* GetHandleSize */
    case 0xA027: SETD(0, set_handle_size(A(0), D(0))); break;      /* ReallocHandle */
    case 0xA029: hflags[slot_of(A(0))] |= 0x80; SETD(0, 0); break;
    case 0xA02A: hflags[slot_of(A(0))] &= ~0x80; SETD(0, 0); break;
    case 0xA049: case 0xA04A: case 0xA02B: case 0xA064: case 0xA067: case 0xA068: SETD(0, 0); break; /* HPurge,HNoPurge,EmptyHandle,MoveHHi,HSet/ClrRBit */
    case 0xA069: SETD(0, hflags[slot_of(A(0))]); break;
    case 0xA06A: hflags[slot_of(A(0))] = D(0) & 0xFF; SETD(0, 0); break;
    case 0xA036: case 0xA063: case 0xA02D: case 0xA04B: case 0xA040: case 0xA04D: SETD(0, 0); break; /* MoreMasters,MaxApplZone,SetApplLimit,SetGrowZone,ResrvMem,PurgeMem */
    case 0xA01C: case 0xA04C: case 0xA061: SETD(0, HEAP_END - heap_ptr); break;   /* FreeMem, CompactMem, MaxBlock */
    case 0xA11D: SETD(0, HEAP_END - heap_ptr); SETA(0, 0); break;                 /* MaxMem */
    case 0xA062: SETD(0, HEAP_END - heap_ptr); SETA(0, HEAP_END - heap_ptr); break;/* PurgeSpace */
    case 0xA065: SETD(0, SP - rd32(0x130)); break;                                 /* StackSpace */
    case 0xA055: break;                                                            /* StripAddress */
    case 0xA02E: case 0xA22E: { memmove(ram + (A(1) & 0xFFFFFF), ram + (A(0) & 0xFFFFFF), D(0)); SETD(0, 0); break; }
    case 0xA9E1: { uint32_t h = A(0), s = slot_of(h); uint32_t nh = new_handle(hsize[s]); memcpy(ram + rd32(nh), ram + rd32(h), hsize[s]); SETA(0, nh); SETD(0, 0); break; }
    case 0xA9E3: { uint32_t nh = new_handle(D(0)); memcpy(ram + rd32(nh), ram + (A(0) & 0xFFFFFF), D(0)); SETA(0, nh); SETD(0, 0); break; }
    case 0xA9E2: case 0xA9EF: { uint32_t h = A(1), s = slot_of(h), old = hsize[s], cnt = D(0); set_handle_size(h, old + cnt); memcpy(ram + rd32(h) + old, ram + (A(0) & 0xFFFFFF), cnt); SETD(0, 0); break; }
    case 0xA9E4: { uint32_t src = A(0), dst = A(1), ss = hsize[slot_of(src)], ds = hsize[slot_of(dst)]; set_handle_size(dst, ds + ss); memcpy(ram + rd32(dst) + ds, ram + rd32(src), ss); SETD(0, 0); break; }
    /* ---- Trap table ---- */
    case 0xA146: case 0xA346: { uint32_t k = trap_key(D(0), (w & 0x200) != 0); SETA(0, patched[k] ? patched[k] : STUB_BASE + k * 4); break; }
    case 0xA047: case 0xA247: { uint32_t k = trap_key(D(0), (w & 0x200) != 0); patched[k] = A(0); if (trace_limit > 0) fprintf(stderr, "  SetTrapAddress key=%03x -> %06x\n", k, A(0)); break; }
    /* ---- Resource Manager ---- */
    case 0xA9A0: { uint32_t id = rd16(sp), type = rd32(sp + 2); Res *r = find_res(type, (int16_t)id); setsp(sp + 6); wr32(sp + 6, r ? res_get_handle(r) : 0); res_err = r ? 0 : -192; break; }
    case 0xA9B8: case 0xA9B9: case 0xA9BA: case 0xA9BB: case 0xA9BC: {      /* GetPattern/Cursor/String/Icon/Picture */
        static const uint32_t ty[] = {0x50415420, 0x43555253, 0x53545220, 0x49434F4E, 0x50494354};
        uint32_t id = rd16(sp); Res *r = find_res(ty[n - 0xA9B8], (int16_t)id); setsp(sp + 2); wr32(sp + 2, r ? res_get_handle(r) : 0); break; }
    case 0xA9A2: case 0xA9A3: case 0xA992: setsp(sp + 4); break;            /* Load/Release/Detach */
    case 0xA9A5: { Res *r = res_from_handle(rd32(sp)); setsp(sp + 4); wr32(sp + 4, r ? r->len : 0xFFFFFFFF); break; }
    case 0xA9A6: { Res *r = res_from_handle(rd32(sp)); setsp(sp + 4); wr16(sp + 4, r ? r->attrs : 0); break; }
    case 0xA9A7: setsp(sp + 6); break;                                      /* SetResAttrs */
    case 0xA9AA: setsp(sp + 4); break;                                      /* ChangedResource */
    case 0xA993: case 0xA998: case 0xA99B: setsp(sp + 2); break;           /* SetResPurge, UseResFile, SetResLoad */
    case 0xA994: setsp(sp); wr16(sp, 1); break;                             /* CurResFile */
    case 0xA9AF: wr16(sp, (uint16_t)res_err); break;                        /* ResError */
    case 0xA9A4: setsp(sp + 4); wr16(sp + 4, 1); break;                     /* HomeResFile */
    case 0xA99C: { uint32_t type = rd32(sp); int c = 0; for (int i = 0; i < nres; i++) if (res[i].type == type) c++; setsp(sp + 4); wr16(sp + 4, c); break; }
    case 0xA99D: { uint32_t type = rd32(sp), idx = rd16(sp + 4); int c = 0; uint32_t h = 0; for (int i = 0; i < nres; i++) if (res[i].type == type && ++c == (int)idx) { h = res_get_handle(&res[i]); break; } setsp(sp + 6); wr32(sp + 6, h); break; }
    case 0xA99E: { int c = 0; uint32_t seen[64]; for (int i = 0; i < nres; i++) { int f = 0; for (int j = 0; j < c; j++) if (seen[j] == res[i].type) f = 1; if (!f && c < 64) seen[c++] = res[i].type; } wr16(sp, c); break; }
    case 0xA997: setsp(sp + 4); wr16(sp + 4, 0xFFFF); res_err = -43; break;  /* OpenResFile (not found) */
    /* ---- Init / environment ---- */
    case 0xA86E: init_graf(rd32(sp)); setsp(sp + 4); break;
    case 0xA97B: case 0xA851: setsp(sp + 4); break;                        /* InitDialogs, SetCursor */
    case 0xA8FE: case 0xA912: case 0xA930: case 0xA9CC: case 0xA850: case 0xA852: case 0xA853: case 0xA856: case 0xA9E6: break;
    case 0xA9C8: case 0xA9E5: setsp(sp + 2); break;                        /* SysBeep, InitPack */
    case 0xA032: SETD(0, 0); break;                                         /* FlushEvents */
    case 0xA03F: break;
    case 0xA975: setsp(sp); wr32(sp, ticks_now()); break;                  /* TickCount */
    case 0xA861: { uint32_t sd = rd32(cur_a5() - 4 - 126); sd = sd * 16807u % 2147483647u; wr32(cur_a5() - 4 - 126, sd); wr16(sp, (int16_t)(sd & 0xFFFF)); break; }
    case 0xA970: case 0xA971: { uint32_t ev = rd32(sp); for (int i = 0; i < 16; i++) wr8(ev + i, 0); wr32(ev + 10, ticks_now()); setsp(sp + 6); wr16(sp + 6, 0); break; }
    case 0xA860: { uint32_t ev = rd32(sp + 8); for (int i = 0; i < 16; i++) wr8(ev + i, 0); wr32(ev + 10, ticks_now()); setsp(sp + 14); wr16(sp + 14, 0); break; }
    case 0xA974: case 0xA973: wr16(sp, 0); break;                           /* Button, StillDown */
    case 0xA972: { uint32_t pt = rd32(sp); wr32(pt, 0); setsp(sp + 4); break; }
    case 0xA090: { uint32_t p = A(0); wr16(p, 2); wr16(p + 2, 6); wr16(p + 4, 0x0710); wr16(p + 6, 3); wr8(p + 8, 1); wr8(p + 9, 1); wr16(p + 10, 3); wr16(p + 12, 0); wr16(p + 14, 0); SETD(0, 0); break; }
    case 0xA1AD: {
        uint32_t sel = D(0), v = 0; int ok = 1;
        switch (sel) { case 0x73797376: v = 0x0710; break; case 0x71642020: v = 0x0300; break; case 0x70726F63: v = 4; break;
                       case 0x66707520: v = 3; break; case 0x6D616368: v = 25; break; case 0x6B626420: v = 3; break; default: ok = 0; }
        SETA(0, v); SETD(0, ok ? 0 : (uint32_t)-5551); break; }
    /* ---- Segment loader / process ---- */
    case 0xA9F0: do_loadseg(ret); redirected = 1; break;
    case 0xA9F4: die("ExitToShell (clean exit)"); redirected = 1; break;
    case 0xA9C9: die("SysError(%d)", (int16_t)D(0)); redirected = 1; break;
    default:
        die("unimplemented trap %04X %s at ret=%06x  (D0=%08x A0=%08x A1=%08x SP=%06x)", w, tname(w), ret, D(0), A(0), A(1), SP);
        redirected = 1; break;
    }
    if (!redirected) W(M68K_REG_PC, ret);
}

