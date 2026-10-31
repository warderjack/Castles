/* ---- VBL + Time Manager tasks, run as 68k subroutines between emulation slices ---- */
#define SENTINEL (STUB_BASE + 0x5000u)
static int call_done = 0, call_depth = 0;
static uint32_t vbl_dummy = 0;
static void vbl_init(void) { if (vbl_dummy) return; vbl_dummy = alloc_block(16); for (int i = 0; i < 16; i += 2) wr16(vbl_dummy + i, 0); wr16(0x160, 0); wr32(0x162, vbl_dummy); wr32(0x166, vbl_dummy); }
static uint32_t tm_tasks[32]; static int ntm = 0; static double tm_wake[32];
static double now_ms(void) { if (det) return det_slices * 0.67; struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }

static uint32_t call_args[4]; static int call_nargs = 0;
static void call68k(uint32_t addr, uint32_t a0, uint32_t a1) {
    if (getenv("TASKLOG")) { fprintf(stderr, "[task] call %06x a0=%x a1=%x bytes:", addr, a0, a1); for (int i = 0; i < 16; i++) fprintf(stderr, "%02x", rd8(addr + i)); fprintf(stderr, "\n"); }
    uint32_t sv[18]; for (int i = 0; i < 8; i++) { sv[i] = R(M68K_REG_D0 + i); sv[8 + i] = R(M68K_REG_A0 + i); } sv[16] = R(M68K_REG_PC); sv[17] = R(M68K_REG_SR);
    uint32_t sp0 = SP; setsp(sp0 - 128);                        /* skip red zone below SP */
    for (int i = 0; i < call_nargs; i++) push32(call_args[i]); call_nargs = 0;
    push32(SENTINEL); W(M68K_REG_A0, a0); W(M68K_REG_A1, a1); W(M68K_REG_PC, addr);
    call_done = 0; int guard = 0; call_depth++;
    while (!call_done && !halted && guard++ < 200) m68k_execute(2000);
    call_depth--;
    for (int i = 0; i < 8; i++) { W(M68K_REG_D0 + i, sv[i]); W(M68K_REG_A0 + i, sv[8 + i]); } setsp(sv[15]); W(M68K_REG_PC, sv[16]); W(M68K_REG_SR, sv[17]);
    if (!call_done) fprintf(stderr, "  [tasks] routine %06x did not return\n", addr);
}
#ifdef HAVE_SDL
static SDL_AudioDeviceID aud_dev; static int aud_tried;
static void snd_out(uint32_t buf, uint32_t hdr) {
    if (headless) return; uint32_t rate = rd32(hdr + 8) >> 16; if (!rate) rate = 22254;
    if (!aud_tried) { aud_tried = 1; SDL_AudioSpec w = {0}, g; w.freq = rate; w.format = AUDIO_U8; w.channels = 1; w.samples = 1024; aud_dev = SDL_OpenAudioDevice(NULL, 0, &w, &g, SDL_AUDIO_ALLOW_ANY_CHANGE);
        if (!aud_dev) fprintf(stderr, "  [snd] no audio device: %s\n", SDL_GetError()); else SDL_PauseAudioDevice(aud_dev, 0); }
    if (!aud_dev) return; uint32_t n = rd32(buf); int ch = rd16(hdr) ? rd16(hdr) : 1; n *= ch; if (n > 65536) n = 65536; if (SDL_GetQueuedAudioSize(aud_dev) > rate * 2) return;
    { static int sl = -1; if (sl < 0) sl = getenv("SNDLOG") != NULL; if (sl) { unsigned mn = 255, mx = 0; for (uint32_t i = 0; i < n; i++) { unsigned v = ram[buf + 16 + i]; if (v < mn) mn = v; if (v > mx) mx = v; } fprintf(stderr, "  [snd] queue %u bytes min=%u max=%u queued=%u\n", n, mn, mx, SDL_GetQueuedAudioSize(aud_dev)); } }
    SDL_QueueAudio(aud_dev, ram + buf + 16, n);
}
#else
static void snd_out(uint32_t buf, uint32_t hdr) {}
#endif
#ifdef HAVE_SDL
static int snd_nodev(void) { return aud_tried && !aud_dev; }
static double snd_queued_ms(uint32_t hdr) { if (!aud_dev) return 0; uint32_t rate = rd32(hdr + 8) >> 16; if (!rate) rate = 22254; return SDL_GetQueuedAudioSize(aud_dev) * 1000.0 / rate; }
#else
static int snd_nodev(void) { return 1; }
static double snd_queued_ms(uint32_t hdr) { return 0; }
#endif
static int fx_st = 0;
static void snd_fx_hook(void) {
    int st = fx_st; static double t0 = 0; const char *nm = getenv("SNDFX"); if (!nm || st > 2) return; if (!SB[13]) return;
    double ms = now_ms(); if (!t0) t0 = ms; if (ms - t0 < (getenv("SNDFXAT") ? atof(getenv("SNDFXAT")) : 20000.0) + st * 3000) return;
    uint32_t p = alloc_block(128); size_t l = strlen(nm); wr8(p, (uint8_t)l); for (size_t i = 0; i <= l; i++) wr8(p + 1 + i, (uint8_t)nm[i]);
    uint32_t arg = getenv("SNDFXP") ? p : p + 1; uint32_t st4 = alloc_block(64); for (int i = 0; i < 64; i += 4) wr32(st4 + i, 0);
    fprintf(stderr, "  [snd] FX hook #%d: call seg13+0xa30 name=%s arg=%06x\n", st, nm, arg); st++; fx_st = st;
    { uint32_t h = rd32(A[5] - 0x2b4e); { uint32_t tb = rd32(A[5] - 0x1122); int cn = (int16_t)rd16(A[5] - 0x1124); fprintf(stderr, "  [snd] FX table=%06x count=%d:", tb, cn); for (int i = 0; i < cn && i < 40; i++) fprintf(stderr, " %d(%x)", (int16_t)rd16(tb + 0x26 * i + 0x1c), rd32(tb + 0x26 * i + 4)); fprintf(stderr, "\n"); } fprintf(stderr, "  [snd] FX drvproc=%06x -117c=%02x -1216=%04x\n", rd32(A[5] - 0x1186), rd8(A[5] - 0x117c), rd16(A[5] - 0x1216)); fprintf(stderr, "  [snd] FX -1078=%04x -111a=%02x ", rd16(A[5] - 0x1078), rd8(A[5] - 0x111a)); fprintf(stderr, "  [snd] FX prefs handle=%06x ptr=%06x byte1b=%02x\n", h, h ? rd32(h) : 0, h ? rd8(rd32(h) + 0x1b) : 0); }
    if (st == 1) { call_args[0] = st4; call_nargs = 1; call68k(SB[13] + 0x54c, 0, 0); fprintf(stderr, "  [snd] FX idlist:"); for (int i = 0; i < 60; i++) { int v = (int16_t)rd16(A[5] - 0x139a + 2 * i); if (v == -1) break; fprintf(stderr, " %d", v); } fprintf(stderr, "\n"); } call_args[0] = 0; call_args[1] = arg; call_args[2] = st4; call_nargs = 3; call68k(SB[13] + 0x96e, 0, 0); if (getenv("SNDVOICE")) { uint32_t b = 0xa84320; { fprintf(stderr, "  [voice-find]"); for (uint32_t q = 0xa80000; q < 0xa90000; q += 2) if (rd32(q) == rd32(rd32(b + 4)) && q != rd32(b + 4)) fprintf(stderr, " %x(b+%x)", q, q - b); fprintf(stderr, "\n"); } fprintf(stderr, "  [voice-now] sel=%u arg=%x err=%04x", rd32(b), rd32(b + 4), rd16(b + 8)); for (int i = 8; i < 11; i++) { uint32_t a1 = b + 0x22d2 + 4 * i; fprintf(stderr, " v%d(p=%x e=%x st=%04x)", i, rd32(a1), rd32(a1 + 0x280), rd16(a1 + 0x200)); } fprintf(stderr, "\n"); }
    { uint32_t tb = rd32(A[5] - 0x1122); int cn = (int16_t)rd16(A[5] - 0x1124); for (int i = 0; i < cn; i++) if ((int16_t)rd16(tb + 0x26 * i + 0x1c) == 1200) { uint32_t r = tb + 0x26 * i; fprintf(stderr, "  [snd] FX rec1200:"); for (int k = 0; k < 0x26; k++) fprintf(stderr, "%02x%s", rd8(r + k), (k & 3) == 3 ? " " : ""); uint32_t dp = rd32(r + 0xc); fprintf(stderr, "\n  [snd] FX data@%06x:", dp); for (int k = 0; k < 32; k++) fprintf(stderr, "%02x", rd8(dp + k)); { unsigned mn = 255, mx = 0; for (uint32_t k = 0; k < rd32(r + 0x10); k++) { unsigned v = rd8(dp + k); if (v < mn) mn = v; if (v > mx) mx = v; } fprintf(stderr, " range %u..%u", mn, mx); } fprintf(stderr, "\n"); } }
}
static void snd_voice_dump(void) {
    static int n = 0; if (!getenv("SNDVOICE") || n > 12) return; static double last = 0; double ms = now_ms(); (void)last; (void)ms; if (!fx_st) return;
    uint32_t b = 0xa84320; fprintf(stderr, "  [voice] 11c0=%u 11c4=%u 1830=%02x", rd16(b + 0x11c0), rd16(b + 0x11c4), rd8(b + 0x1830)); for (int i = 0; i < 11; i++) { uint32_t a1 = b + 0x22d2 + 4 * i; fprintf(stderr, " v%d(p=%x e=%x st=%04x pos=%x)", i, rd32(a1), rd32(a1 + 0x280), rd16(a1 + 0x200), rd32(a1 + 0x2c0)); } fprintf(stderr, "\n"); n++;
}
static void snd_tick(void) {
    snd_fx_hook(); snd_voice_dump();
    /* keep ~300 ms of audio queued ahead in SDL; the buffer is free as soon as it has been copied, so refill immediately */
    if (!dbp.on) return; uint32_t hdr = dbp.hdr; double ms = now_ms();
    if (headless || snd_nodev()) { /* no device: keep the original real-time pacing so guest timing matches */
        uint32_t buf = rd32(hdr + 12 + 4 * dbp.cur);
        if (dbp.t_end < 0) { if (!(rd32(buf + 4) & 1)) { dbp.on = 0; return; } dbp.t_end = ms + snd_buf_ms(buf, hdr); return; }
        for (int g = 0; g < 4 && ms >= dbp.t_end; g++) {
            uint32_t fl = rd32(buf + 4);
            if (fl & 4) { dbp.on = 0; return; }
            wr32(buf + 4, fl & ~1u); call_args[0] = dbp.chan; call_args[1] = buf; call_nargs = 2; call68k(rd32(hdr + 20), 0, 0);
            dbp.cur ^= 1; buf = rd32(hdr + 12 + 4 * dbp.cur);
            if (rd32(buf + 4) & 1) dbp.t_end += snd_buf_ms(buf, hdr); else { dbp.t_end = ms + 1; break; }
        }
        return;
    }
    for (int g = 0; g < 8 && dbp.on && (!aud_tried || snd_queued_ms(hdr) < 300); g++) {
        uint32_t buf = rd32(hdr + 12 + 4 * dbp.cur), fl = rd32(buf + 4);
        if (!(fl & 1)) { if (dbp.t_end < 0) dbp.on = 0; break; }     /* not ready: wait (or never started) */
        dbp.t_end = 0; snd_out(buf, hdr);
        if (fl & 4) { dbp.on = 0; fprintf(stderr, "  [snd] double buffer finished\n"); return; }
        wr32(buf + 4, fl & ~1u); call_args[0] = dbp.chan; call_args[1] = buf; call_nargs = 2; call68k(rd32(hdr + 20), 0, 0);
        dbp.cur ^= 1;
    }
}
static void run_tasks(void) {
    snd_tick();
    static uint32_t last_tick = 0; uint32_t t = ticks_now(); int n = (int)(t - last_tick); if (!last_tick) n = 0; last_tick = t; if (n > 4) n = 4;
    vbl_init();
    while (n-- > 0) for (uint32_t task = rd32(0x162), g = 0; task && g < 64; task = rd32(task), g++) { int cnt = (int16_t)rd16(task + 10);
        if (task != vbl_dummy && cnt > 0) { cnt--; wr16(task + 10, cnt); if (cnt == 0) call68k(rd32(task + 6), task, 0); } }
    double ms = now_ms();
    for (int i = 0; i < ntm; i++) if (tm_wake[i] > 0 && ms >= tm_wake[i]) { uint32_t tk = tm_tasks[i]; tm_wake[i] = 0; call68k(rd32(tk + 6), 0, tk); }
}
static int tasks_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    switch (n) {
    case 0xA033: { vbl_init(); uint32_t t = A(0), tail = rd32(0x166); wr32(t, 0); wr16(t + 4, 1); wr32(tail, t); wr32(0x166, t); } SETD(0, 0); if (trace_limit > 0) fprintf(stderr, "  VInstall %08x addr=%06x count=%d\n", A(0), rd32(A(0) + 6), (int16_t)rd16(A(0) + 10)); return 1;
    case 0xA034: { vbl_init(); uint32_t prev = 0, t = rd32(0x162); while (t && t != A(0)) { prev = t; t = rd32(t); }
        if (t) { if (prev) wr32(prev, rd32(t)); else wr32(0x162, rd32(t)); if (rd32(0x166) == t) wr32(0x166, prev); } SETD(0, 0); return 1; }
    case 0xA058: { int f = 0; for (int i = 0; i < ntm; i++) if (tm_tasks[i] == A(0)) { tm_wake[i] = 0; f = 1; } if (!f && ntm < 32) { tm_tasks[ntm] = A(0); tm_wake[ntm++] = 0; } } SETD(0, 0); return 1;                                   /* InsTime */
    case 0xA059: { for (int i = 0; i < ntm; i++) if (tm_tasks[i] == A(0)) { tm_tasks[i] = tm_tasks[--ntm]; tm_wake[i] = tm_wake[ntm]; i--; } SETD(0, 0); return 1; }                       /* RmvTime */
    case 0xA05A: { int32_t c = (int32_t)D(0); double ms = c >= 0 ? c : -c / 1000.0; int f = 0; for (int i = 0; i < ntm; i++) if (tm_tasks[i] == A(0)) { tm_wake[i] = now_ms() + (ms < 1 ? 1 : ms); f = 1; } if (!f && ntm < 32) { tm_tasks[ntm] = A(0); tm_wake[ntm++] = now_ms() + (ms < 1 ? 1 : ms); } SETD(0, 0); return 1; } /* PrimeTime */
    default: return 0;
    }
}
