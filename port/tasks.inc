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

/* ---- Sound Manager file play (SndStartFilePlay): the game streams 'IntroSong' (AIFF) through its own channel; play it on a second SDL device ---- */
static struct { int on; uint32_t chan, completion; double t_end; int ndone; uint8_t *pcm; size_t len; int has_dev;
#ifdef HAVE_SDL
    SDL_AudioDeviceID dev;
#endif
} fp;
static double aiff_rate80(const uint8_t *p) { int e = ((p[0] & 0x7F) << 8) | p[1]; uint64_t m = 0; for (int i = 0; i < 8; i++) m = (m << 8) | p[2 + i]; return e == 0 && m == 0 ? 0 : ldexp((double)m, e - 16383 - 63); }
static int fp_chan_match(uint32_t chan) { return fp.on && chan == fp.chan; }
static void filep_stop(void) {
    fp.on = 0;
#ifdef HAVE_SDL
    if (fp.dev) { SDL_ClearQueuedAudio(fp.dev); SDL_CloseAudioDevice(fp.dev); fp.dev = 0; }
#endif
    free(fp.pcm); fp.pcm = NULL;
}
static int filep_start(uint32_t chan, int fref, uint32_t completion) {
    OF *o = getof(fref); if (!o || !o->f) { fprintf(stderr, "  [snd] SndStartFilePlay: bad refnum %d\n", fref); return -43; }
    filep_stop();
    long len = o->len; if (len <= 0 || len > (64L << 20)) { fseek(o->f, 0, SEEK_END); len = ftell(o->f) - o->base; }
    uint8_t *d = malloc(len); if (!d) return -108; fseek(o->f, o->base, SEEK_SET); size_t got = fread(d, 1, len, o->f); 
    int ch = 1, bits = 16; uint32_t frames = 0; double rate = 22254; size_t off = 0, n = 0;
    if (got < 12 || memcmp(d, "FORM", 4)) { fprintf(stderr, "  [snd] SndStartFilePlay: not an AIFF file\n"); free(d); return -50; }
    for (size_t p = 12; p + 8 <= got;) { uint32_t sz = (d[p+4] << 24) | (d[p+5] << 16) | (d[p+6] << 8) | d[p+7]; const uint8_t *c = d + p + 8;
        if (!memcmp(d + p, "COMM", 4) && p + 8 + 18 <= got) { ch = (c[0] << 8) | c[1]; frames = (c[2] << 24) | (c[3] << 16) | (c[4] << 8) | c[5]; bits = (c[6] << 8) | c[7]; rate = aiff_rate80(c + 8); }
        else if (!memcmp(d + p, "SSND", 4)) { uint32_t so = (c[0] << 24) | (c[1] << 16) | (c[2] << 8) | c[3]; off = p + 16 + so; n = sz >= 8 + so ? sz - 8 - so : 0; if (off + n > got) n = off < got ? got - off : 0; }
        p += 8 + sz + (sz & 1); }
    if (!n || !ch) { fprintf(stderr, "  [snd] SndStartFilePlay: no sound data\n"); free(d); return -50; }
    (void)frames; fprintf(stderr, "  [snd] SndStartFilePlay: AIFF %d ch %d-bit %.0f Hz %zu bytes (%.1f s)\n", ch, bits, rate, n, n / (double)(ch * (bits / 8)) / rate);
    fp.pcm = malloc(n); memcpy(fp.pcm, d + off, n); fp.len = n; free(d);
    fp.chan = chan; fp.completion = completion; fp.on = 1; fp.t_end = now_ms() + n * 1000.0 / (ch * (bits / 8) * rate);
#ifdef HAVE_SDL
    if (!headless) { SDL_AudioSpec w = {0}, g; w.freq = (int)rate; w.format = bits == 8 ? AUDIO_S8 : AUDIO_S16MSB; w.channels = ch; w.samples = 2048;
        fp.dev = SDL_OpenAudioDevice(NULL, 0, &w, &g, 0);
        if (fp.dev) { SDL_QueueAudio(fp.dev, fp.pcm, (Uint32)n); SDL_PauseAudioDevice(fp.dev, 0); } else fprintf(stderr, "  [snd] no audio device for file play: %s\n", SDL_GetError()); }
#endif
    return 0;
}
static void filep_poll(void) {
    if (!fp.on) return; int done = 0;
#ifdef HAVE_SDL
    if (fp.dev) done = SDL_GetQueuedAudioSize(fp.dev) == 0; else
#endif
    done = now_ms() >= fp.t_end;
    if (done) { uint32_t comp = fp.completion, chan = fp.chan; filep_stop(); if (comp) { call_args[0] = chan; call_nargs = 1; call68k(comp, 0, 0); } }
}
static void snd_tick_body(void);
static int snd_in_tick;
static void snd_tick(void) {
    /* The mixer keeps its voice-loop counter in driver memory ($2952(a4)), so it must never run nested inside itself: a poll fired from one of its own loop back-edges
       would reset the counter and the outer pass would wrap the 16-bit counter and clear the heap. */
    if (snd_in_tick) { if (getenv("SNDLOG")) fprintf(stderr, "  [snd] nested snd_tick skipped\n"); return; }
    snd_in_tick = 1; snd_tick_body(); snd_in_tick = 0;
}
static void cd_stop_service(void) {
    /* CD release sound object (CODE 1 0x5197x): to stop a streamed voice the game sets stop-request ($44a) and spins at 0xa51a12..a51a1a until its double-buffer callback acknowledges ($44b).
       If that stream is no longer being pumped (it finished, or another double buffer took over), nothing would ever call the callback: run it here; if that does not release the loop, acknowledge by hand. */
    static double t_in = 0; static int nsvc = 0;
    uint32_t pc = R(M68K_REG_PC); if (pc < 0xa51a12 || pc > 0xa51a1c) { t_in = 0; nsvc = 0; return; }
    double now = now_ms(); if (!t_in) t_in = now;
    uint32_t a2 = R(M68K_REG_A2), hdr = a2 + 0x432, proc = rd32(hdr + 20), chan = rd32(a2 + 0x42e);
    if (rd8(a2 + 0x44b)) return;
    if (nsvc < 3 && proc && chan && rd8(a2 + 0x44a)) {
        uint32_t buf = rd32(hdr + 12); for (int k = 0; k < 2; k++) { uint32_t b = rd32(hdr + 12 + 4 * k); if (!(rd32(b + 4) & 1)) { buf = b; break; } }
        nsvc++; fprintf(stderr, "  [snd] servicing voice stop handshake (proc=%06x obj=%06x)\n", proc, a2); call_args[0] = chan; call_args[1] = buf; call_nargs = 2; call68k(proc, 0, 0); return;
    }
    if (now - t_in > 700) { fprintf(stderr, "  [snd] voice stop wait did not clear (obj=%06x 44a=%d proc=%06x chan=%06x): acknowledging by hand\n", a2, rd8(a2 + 0x44a), proc, chan); wr8(a2 + 0x44b, 1); t_in = 0; nsvc = 0; }
}
static void snd_tick_body(void) {
    snd_fx_hook(); snd_voice_dump();
    if (cd_mode) cd_stop_service();
    filep_poll();
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
