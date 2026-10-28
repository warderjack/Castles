/* ---- VBL + Time Manager tasks, run as 68k subroutines between emulation slices ---- */
#define SENTINEL (STUB_BASE + 0x5000u)
static int call_done = 0;
static uint32_t vbl_dummy = 0;
static void vbl_init(void) { if (vbl_dummy) return; vbl_dummy = alloc_block(16); for (int i = 0; i < 16; i += 2) wr16(vbl_dummy + i, 0); wr16(0x160, 0); wr32(0x162, vbl_dummy); wr32(0x166, vbl_dummy); }
static uint32_t tm_tasks[32]; static int ntm = 0; static double tm_wake[32];
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }

static uint32_t call_args[2]; static int call_nargs = 0;
static void call68k(uint32_t addr, uint32_t a0, uint32_t a1) {
    uint32_t sv[18]; for (int i = 0; i < 8; i++) { sv[i] = R(M68K_REG_D0 + i); sv[8 + i] = R(M68K_REG_A0 + i); } sv[16] = R(M68K_REG_PC); sv[17] = R(M68K_REG_SR);
    uint32_t sp0 = SP; setsp(sp0 - 128);                        /* skip red zone below SP */
    for (int i = 0; i < call_nargs; i++) push32(call_args[i]); call_nargs = 0;
    push32(SENTINEL); W(M68K_REG_A0, a0); W(M68K_REG_A1, a1); W(M68K_REG_PC, addr);
    call_done = 0; int guard = 0;
    while (!call_done && !halted && guard++ < 200) m68k_execute(2000);
    for (int i = 0; i < 8; i++) { W(M68K_REG_D0 + i, sv[i]); W(M68K_REG_A0 + i, sv[8 + i]); } setsp(sv[15]); W(M68K_REG_PC, sv[16]); W(M68K_REG_SR, sv[17]);
    if (!call_done) fprintf(stderr, "  [tasks] routine %06x did not return\n", addr);
}
static void snd_tick(void) {
    if (!dbp.on) return; double ms = now_ms(); uint32_t hdr = dbp.hdr, buf = rd32(hdr + 12 + 4 * dbp.cur);
    if (dbp.t_end < 0) { if (!(rd32(buf + 4) & 1)) { dbp.on = 0; return; } dbp.t_end = ms + snd_buf_ms(buf, hdr); return; }
    for (int g = 0; g < 4 && ms >= dbp.t_end; g++) {
        uint32_t fl = rd32(buf + 4);
        if (fl & 4) { dbp.on = 0; fprintf(stderr, "  [snd] double buffer finished\n"); return; }
        wr32(buf + 4, fl & ~1u); call_args[0] = dbp.chan; call_args[1] = buf; call_nargs = 2; call68k(rd32(hdr + 20), 0, 0);
        dbp.cur ^= 1; buf = rd32(hdr + 12 + 4 * dbp.cur);
        if (rd32(buf + 4) & 1) dbp.t_end += snd_buf_ms(buf, hdr); else { dbp.t_end = ms + 1; break; }
    }
}
static void run_tasks(void) {
    snd_tick();
    static uint32_t last_tick = 0; uint32_t t = ticks_now(); int n = (int)(t - last_tick); if (!last_tick) n = 0; last_tick = t; if (n > 4) n = 4;
    vbl_init();
    while (n-- > 0) for (uint32_t task = rd32(0x162), g = 0; task && g < 64; task = rd32(task), g++) { int cnt = (int16_t)rd16(task + 10);
        if (task != vbl_dummy && cnt > 0) { cnt--; wr16(task + 10, cnt); if (cnt == 0) call68k(rd32(task + 6), task, 0); } }
    double ms = now_ms();
    for (int i = 0; i < ntm; i++) if (tm_wake[i] > 0 && ms >= tm_wake[i]) { tm_wake[i] = 0; call68k(rd32(tm_tasks[i] + 6), 0, tm_tasks[i]); }
}
static int tasks_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    switch (n) {
    case 0xA033: { vbl_init(); uint32_t t = A(0), tail = rd32(0x166); wr32(t, 0); wr16(t + 4, 1); wr32(tail, t); wr32(0x166, t); } SETD(0, 0); if (trace_limit > 0) fprintf(stderr, "  VInstall %08x addr=%06x count=%d\n", A(0), rd32(A(0) + 6), (int16_t)rd16(A(0) + 10)); return 1;
    case 0xA034: { vbl_init(); uint32_t prev = 0, t = rd32(0x162); while (t && t != A(0)) { prev = t; t = rd32(t); }
        if (t) { if (prev) wr32(prev, rd32(t)); else wr32(0x162, rd32(t)); if (rd32(0x166) == t) wr32(0x166, prev); } SETD(0, 0); return 1; }
    case 0xA058: if (ntm < 32) { tm_tasks[ntm] = A(0); tm_wake[ntm++] = 0; } SETD(0, 0); return 1;                                   /* InsTime */
    case 0xA059: { for (int i = 0; i < ntm; i++) if (tm_tasks[i] == A(0)) tm_wake[i] = 0; SETD(0, 0); return 1; }                       /* RmvTime */
    case 0xA05A: { int32_t c = (int32_t)D(0); double ms = c >= 0 ? c : -c / 1000.0; for (int i = 0; i < ntm; i++) if (tm_tasks[i] == A(0)) tm_wake[i] = now_ms() + (ms < 1 ? 1 : ms); SETD(0, 0); return 1; } /* PrimeTime */
    default: return 0;
    }
}
