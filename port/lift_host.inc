/* ---- glue between translated code (gen/lift_*.c) and the interpreter / HLE ---- */
static void hle(uint16_t w, uint32_t ret);
u32 D[8], A[8], SB[16], REL_A4, REL_A5; u32 fX, fN, fZ, fV, fC;
static jmp_buf lift_jb; static u32 lift_bail_pc; static int lift_synced, lift_active, lift_on = 1;
u32 lift_mid, lift_mid_r0; static const LiftMid **lift_midtab[16];
static int lift_stubcall;
static LiftFn *lift_segtab[16]; static u32 lift_seglen[16]; static u32 lift_lo = 0xFFFFFFFF, lift_hi = 0;
unsigned long lift_trapbail[65536]; unsigned long lift_enterbail; unsigned lift_ebt[1 << 23]; int lift_dry = 0; static u32 lift_trap_pc; static int chk_partial; static unsigned long chk_part_n; static unsigned long lift_calls, lift_bails, lift_traps;

static void lift_sync_in(void) {
    for (int i = 0; i < 8; i++) { D[i] = R(M68K_REG_D0 + i); A[i] = R(M68K_REG_A0 + i); }
    A[7] = SP; uint32_t sr = R(M68K_REG_SR); fX = (sr >> 4) & 1; fN = (sr >> 3) & 1; fZ = (sr >> 2) & 1; fV = (sr >> 1) & 1; fC = sr & 1;
}
static void lift_sync_out(void) {
    for (int i = 0; i < 7; i++) { W(M68K_REG_D0 + i, D[i]); W(M68K_REG_A0 + i, A[i]); }
    W(M68K_REG_D7, D[7]); setsp(A[7]);
    uint32_t sr = R(M68K_REG_SR) & ~0x1Fu; sr |= (fX << 4) | (fN << 3) | (fZ << 2) | (fV << 1) | fC; W(M68K_REG_SR, sr);
}
static int lift_skipped(int seg, u32 off) { static char *list; static int inited; if (!inited) { inited = 1; const char *e = getenv("LIFTSKIP"); if (e) { FILE *f = fopen(e, "r"); if (f) { fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f); list = calloc(n + 2, 1); fread(list, 1, n, f); fclose(f); } } } if (!list) return 0; char b[32]; snprintf(b, sizeof b, "\n%d:%x\n", seg, off); char *t = malloc(strlen(list) + 4); sprintf(t, "\n%s\n", list); int r = strstr(t, b) != NULL; free(t); return r; }
static void lift_register_seg(int segno, uint32_t base, uint32_t len) {
    if (segno < 0 || segno > 15 || !lift_nents) return;
    SB[segno] = base; lift_seglen[segno] = len; free(lift_segtab[segno]); lift_segtab[segno] = calloc(len / 2 + 2, sizeof(LiftFn));
    for (int i = 0; i < lift_nents; i++) if (lift_ents[i].seg == segno && lift_ents[i].off < len && !lift_skipped(segno, lift_ents[i].off)) lift_segtab[segno][lift_ents[i].off >> 1] = lift_ents[i].fn;
    free(lift_midtab[segno]); lift_midtab[segno] = calloc(len / 2 + 2, sizeof(LiftMid *));
    for (int i = 0; i < lift_nmids; i++) if (lift_mids[i].seg == segno && lift_mids[i].off < len) { const LiftMid **sl = &lift_midtab[segno][lift_mids[i].off >> 1]; if (!*sl || !(*sl)->haslink || lift_mids[i].haslink) *sl = &lift_mids[i]; }
    if (base < lift_lo) lift_lo = base; if (base + len > lift_hi) lift_hi = base + len;
    if (!REL_A4) { REL_A4 = R(M68K_REG_A4); REL_A5 = R(M68K_REG_A5); fprintf(stderr, "[lift] REL_A4=%x REL_A5=%x\n", REL_A4, REL_A5); }
}
static const LiftMid *lift_midlookup(uint32_t pc) {
    if (pc < lift_lo || pc >= lift_hi || (pc & 1)) return NULL;
    for (int s = 1; s < 16; s++) if (lift_midtab[s] && pc >= SB[s] && pc < SB[s] + lift_seglen[s]) return lift_midtab[s][(pc - SB[s]) >> 1];
    return NULL;
}
static LiftFn lift_lookup(uint32_t pc) {
    if (pc < lift_lo || pc >= lift_hi) return NULL;
    for (int s = 1; s < 16; s++) if (lift_segtab[s] && pc >= SB[s] && pc < SB[s] + lift_seglen[s] && !(pc & 1)) return lift_segtab[s][(pc - SB[s]) >> 1];
    return NULL;
}
int lift_loopcnt; static uint32_t lift_last_present;
unsigned lift_bh[1 << 23]; u32 lift_rs[256]; int lift_rsp; u32 lift_ret_pc;
void lift_bail(u32 pc) { if (!lift_dry && pc < (1u << 24)) lift_bh[pc >> 1]++; if (lift_dry) { lift_trap_pc = pc; longjmp(lift_jb, 3); } if (getenv("LIFTLOG")) fprintf(stderr, "[lift] bail to %06x sp=%x\n", pc, A[7]); lift_bail_pc = pc; lift_bails++; longjmp(lift_jb, 1); }
extern u32 lift_mm_r0; void lift_ret_mismatch2(u32 r, u32 r0) { lift_mm_r0 = r0; lift_ret_mismatch(r); }
u32 lift_mm_r0; const char *lift_mm_fn;
void lift_ret_mismatch(u32 r) { static int n; if (getenv("RMLOG") && (r == 0x57e5a || r == 0x57e9c) && n++ < 6) fprintf(stderr, "[retmm] fn=%s r0=%06x r=%06x rsp=%d top=%06x A7=%x\n", lift_mm_fn, lift_mm_r0, r, lift_rsp, lift_rsp ? lift_rs[(lift_rsp-1)&255] : 0, A[7]); lift_bail(r); }
static u32 lift_vis[512]; static int lift_nvis;
void lift_note(LiftFn f) { if (lift_nvis >= 512) return; for (int i = 0; i < lift_nents; i++) if (lift_ents[i].fn == f) { lift_vis[lift_nvis++] = SB[lift_ents[i].seg] + lift_ents[i].off; return; } }
void lift_poll(u32 resume_pc) {
    lift_loopcnt = 0; if (lift_dry) return; { static int np = -1; if (np < 0) np = getenv("NOPOLL") != NULL; if (np) return; }
    static uint32_t last_t; uint32_t t = ticks_now(); if (t == last_t) return; last_t = t;
    lift_sync_out();
    wr32(0x16A, t); wr32(0x20C, (uint32_t)(time(NULL) + 2082844800u));
    run_tasks(); ev_pump();
    if (t - lift_last_present >= 2) { plat_present(); gfx_dirty = 0; lift_last_present = t; }
    if (quit_requested || halted) { lift_synced = 1; lift_bail_pc = resume_pc; lift_bails++; longjmp(lift_jb, 1); }
    lift_sync_in();
}
u32 lift_get_sr(void) { return (R(M68K_REG_SR) & ~0x1Fu) | (fX << 4) | (fN << 3) | (fZ << 2) | (fV << 1) | fC; }
void lift_set_sr(u32 v) { W(M68K_REG_SR, v & 0xFFE0u); fX = (v >> 4) & 1; fN = (v >> 3) & 1; fZ = (v >> 2) & 1; fV = (v >> 1) & 1; fC = v & 1; }
u32 lift_entring[32]; unsigned lift_entri;
void lift_enter(u32 tgt) {
    lift_entring[lift_entri++ & 31] = tgt;
    for (int k = 0; k < 4 && m16(tgt) == 0x4EF9; k++) tgt = m32(tgt + 2);      /* follow jump-table stubs */
    LiftFn f = lift_lookup(tgt);
    if (!f && lift_mdrv_len && lift_nents && tgt < 0xC00000 && !lift_segtab[14] && !memcmp(ram + tgt, lift_mdrv_sig, 16)) { fprintf(stderr, "  [mdrv] code block at %06x\n", tgt); lift_register_seg(14, tgt, lift_mdrv_len); f = lift_lookup(tgt); }
    if (!f && lift_smod_len && lift_nents && tgt < 0xC00000 && !lift_segtab[15] && !memcmp(ram + tgt, lift_smod_sig, 16)) { fprintf(stderr, "  [smod] code block at %06x\n", tgt); lift_register_seg(15, tgt, lift_smod_len); f = lift_lookup(tgt); }
    if (!f && tgt >= 0xC00000) {   /* A5-world dispatch thunks: bra.s/nop ladder into  jsr d16(a5); jmp (a0) */
        u32 t = tgt; for (int k = 0; k < 64; k++) { u16 w = m16(t); if ((w >> 8) == 0x60 && (w & 0xFF) && (w & 0xFF) != 0xFF) t = t + 2 + (s32)(s8)(w & 0xFF); else if (w == 0x4E71) t += 2; else break; }
        if (m16(t) == 0x4EAD && m16(t + 4) == 0x4ED0) { A[7] -= 4; w32(A[7], t + 4); lift_rs[lift_rsp++ & 255] = t + 4; lift_enter(A[5] + (u32)(s32)(s16)m16(t + 2)); lift_rsp--; lift_enter(A[0]); return; }
    }
    if (!f && !lift_dry && tgt >= STUB_BASE && tgt < STUB_END && (m16(tgt) & 0xF000) == 0xA000) {   /* ROM trap stub: A-line + rts */
        u32 ret = m32(A[7]); A[7] += 4; lift_stubcall = 1; lift_trap(m16(tgt), tgt, ret); if (m16(tgt) != 0xA9F0) lift_ret_pc = ret; return; }
    if (!f && !lift_dry && m16(tgt) == 0x6104 && m16(tgt + 6) == 0x225F && m16(tgt + 8) == 0x2251 && m16(tgt + 10) == 0x4ED1) { lift_enter(m32(tgt + 2)); return; }   /* heap dispatch thunk: bsr.s; dc.l target; movea (sp)+,a1; movea (a1),a1; jmp (a1) */
    if (!f && !lift_dry && tgt >= 0xC00000 && m16(tgt) == 0x3F3C && m16(tgt + 4) == 0xA9F0) {                /* unloaded-segment jump-table stub: move.w #seg,-(sp); _LoadSeg */
        A[7] -= 2; w16(A[7], m16(tgt + 2)); lift_trap(0xA9F0, tgt + 4, tgt + 6); return; }
    if (lift_dry && lift_nvis < 512) lift_vis[lift_nvis++] = tgt;
    { static int sc = -1; if (sc < 0) sc = getenv("SNDCMD") != NULL; if (sc && lift_segtab[2] && tgt == SB[2] + 0x14ae && !lift_dry) fprintf(stderr, "  [sndcmd] cmd=%04x w6=%04x l2=%08x\n", m16(A[7] + 8), m16(A[7] + 6), m32(A[7] + 2)); }
    if (f) { lift_calls++; f(); } else { lift_enterbail++; if (tgt < (1u<<24)) lift_ebt[tgt >> 1]++; lift_bail(tgt); }
}
unsigned long lift_blocked, lift_guestcalls;
void lift_trap(u32 w, u32 tpc, u32 ret) {
    if (lift_dry) { lift_trap_pc = tpc; longjmp(lift_jb, 2); }
    if (det && ntraps >= det_next && !call_depth) lift_bail(tpc);
    int fromstub = lift_stubcall; lift_stubcall = 0;
    if (!fromstub && patched[trap_key(w, 0)]) { A[7] -= 4; w32(A[7], ret); lift_enter(patched[trap_key(w, 0)]); return; }
    lift_traps++; lift_sync_out(); idle_hint = 0; trap_pc = tpc; trap_from_stub = 0; wm_redirected = 0;
    hle((uint16_t)w, ret);
    { static int nb = -1; if (nb < 0) nb = getenv("NOBLOCK") != NULL;
      while (!nb && !halted && !quit_requested) {
        u32 pc3 = R(M68K_REG_PC);
        if (pc3 == YIELD_PC) { lift_blocked++; usleep(1500); lift_sync_in(); lift_poll(tpc); lift_sync_out(); }                 /* blocking trap: wait in C */
        else if (pc3 != ret && rd32(R(M68K_REG_A7)) == GCONT_PC) {                                                              /* handler wants a guest proc run (filter / user item) */
            lift_guestcalls++; typeof(gc) gcs = gc; if (getenv("GCLOG")) fprintf(stderr, "[gc] trap=%04x proc=%06x sp=%x\n", w & 0xFFFF, pc3, R(M68K_REG_A7)); lift_sync_in(); lift_rs[lift_rsp++ & 255] = GCONT_PC; lift_enter(pc3); lift_rsp--; lift_sync_out(); if (getenv("GCLOG")) fprintf(stderr, "[gc] done sp=%x res=%04x\n", R(M68K_REG_A7), rd16(R(M68K_REG_A7))); gc = gcs; gc_resume(); }
        else break;
        W(M68K_REG_PC, ret); trap_pc = tpc; trap_from_stub = 0; hle((uint16_t)w, ret); } }
    if (idle_hint && !lift_dry) { static int ns = -1; if (ns < 0) ns = getenv("NOIDLESLEEP") != NULL; idle_hint = 0; if (!ns) usleep(1000); }   /* the game polled for events and found none: yield the CPU like WaitNextEvent would */
    if (!(w & 0x800) && !halted) { uint32_t d0 = R(M68K_REG_D0) & 0xFFFF; uint32_t sr = R(M68K_REG_SR) & ~0xFu; if (d0 == 0) sr |= 4; if (d0 & 0x8000) sr |= 8; W(M68K_REG_SR, sr); }
    uint32_t pc2 = R(M68K_REG_PC);
    if (w == 0xA9F0 && pc2 != ret && !halted && pc2 != YIELD_PC && pc2 != GCONT_PC) { lift_sync_in(); lift_enter(pc2); return; }   /* LoadSeg jumps to the (now loaded) entry */
    if (pc2 != ret || halted) { lift_trapbail[w & 0xFFFF]++; lift_synced = 1; lift_bail_pc = pc2; lift_bails++; longjmp(lift_jb, 1); }
    lift_sync_in();
}
static void lift_check_finish(void);
static int lift_chk_max = 5; static int chk_ok, chk_bad, chk_skip; static int lift_try_check(uint32_t pc, LiftFn f, u32 midoff, u32 midr0); static int lift_check = 0, lift_chk_active;
/* called from the instruction hook when pc is a translated routine's entry; returns 1 if it ran */
static int lift_midlog;
static int lift_nomidpc(uint32_t pc) { { static int ri; static unsigned lo, hi; if (!ri) { ri = 1; const char *e = getenv("NOMIDRANGE"); if (e) sscanf(e, "%x:%x", &lo, &hi); } if (hi && pc >= lo && pc < hi) return 1; } static char *list; static int inited; if (!inited) { inited = 1; const char *e = getenv("NOMIDPC"); if (e) list = strdup(e); } if (!list) return 0; char buf[16]; snprintf(buf, sizeof buf, "%06x", pc); return strstr(list, buf) != NULL; }
static uint32_t lift_dead_pcs[64]; static int lift_ndead;
static int lift_middead(uint32_t pc) { for (int i = 0; i < lift_ndead; i++) if (lift_dead_pcs[i] == pc) return 1; return 0; }
static int lift_check;
static u32 lift_pend; static void lift_pend_fn(void) {
    if (lift_pend < 0xC00000 && m16(lift_pend) == 0x4EB9) { u32 p = lift_pend; A[7] -= 4; w32(A[7], p + 6); lift_rs[lift_rsp++ & 255] = p + 6; lift_enter(m32(p + 2)); lift_rsp--; lift_enter(m32(p + 8)); return; }   /* jsr abs; jmp abs glue pair */
    if (lift_pend < 0xC00000 && m16(lift_pend) == 0x4EF9) { lift_enter(m32(lift_pend + 2)); return; }
    if (lift_pend >= 0xC00000 && m16(lift_pend) == 0x4ED0) lift_enter(A[0]); else lift_enter(lift_pend); }
static int lift_special(uint32_t pc) {      /* non-segment code the lifted runtime can still enter natively: ROM trap stubs, heap dispatch thunks, unloaded-segment stubs */
    if (lift_check || lift_dry || !lift_on || (pc & 1) || pc >= 0xC10000 + 0x1000) return 0;
    if (pc >= STUB_BASE && pc < STUB_END) return (m16(pc) & 0xF000) == 0xA000;
    if (m16(pc) == 0x6104 && m16(pc + 6) == 0x225F && m16(pc + 8) == 0x2251 && m16(pc + 10) == 0x4ED1) return 1;
    if (pc < 0xC00000) return (m16(pc) == 0x4EB9 && m16(pc + 6) == 0x4EF9 && m32(pc + 8) >= STUB_BASE && m32(pc + 8) < STUB_END) || (m16(pc) == 0x4EF9 && m32(pc + 2) >= STUB_BASE && m32(pc + 2) < STUB_END && m16(pc - 6) == 0x4EB9);
    if (m16(pc) == 0x4ED0) return 1;               /* second half of an A5 dispatch thunk: jmp (a0) */
    if (m16(pc) == 0x3F3C && m16(pc + 4) == 0xA9F0) return 1;
    u32 t = pc; for (int k = 0; k < 4 && m16(t) == 0x4EF9; k++) t = m32(t + 2);
    if (t < 0xC00000 && lift_lookup(t)) return 1;
    for (int k = 0; k < 64; k++) { u16 w = m16(t); if ((w >> 8) == 0x60 && (w & 0xFF) && (w & 0xFF) != 0xFF) t = t + 2 + (s32)(s8)(w & 0xFF); else if (w == 0x4E71) t += 2; else break; }
    return m16(t) == 0x4EAD && m16(t + 4) == 0x4ED0;
}
static int lift_try(uint32_t pc) {
    static long lmax = -2; if (lmax == -2) { const char *e = getenv("LIFTMAX"); lmax = e ? atol(e) : -1; }
    LiftFn f = lift_lookup(pc); const LiftMid *mm = NULL; u32 midoff = 0, midr0 = 0;
    if (!lift_midlog) lift_midlog = getenv("MIDLOG") ? 1 : 2;
    if (!f && lift_special(pc)) { lift_pend = pc; f = lift_pend_fn; }
    if (!f && lift_on && !getenv("NOMID")) { mm = lift_midlookup(pc); if (mm && (lift_nomidpc(pc) || lift_middead(pc))) mm = NULL; if (mm && (mm->haslink || !getenv("MIDLINK"))) { f = mm->fn; midoff = pc - SB[mm->seg]; midr0 = m32(R(M68K_REG_A6) + 4); } }
    if (midoff && getenv("RMLOG") && midr0 < 0x10000) { static int nn; if (nn++ < 6) fprintf(stderr, "[midbad] pc=%06x midr0=%x A6=%x A7=%x m32(A6)=%x\n", pc, midr0, R(M68K_REG_A6), R(M68K_REG_A7), m32(R(M68K_REG_A6))); }
    if (midoff && lift_midlog == 1) { fprintf(stderr, "[mid] %06x\n", pc); }
    if (!f || !lift_on) return 0;
    if (lmax >= 0 && (long)lift_calls >= lmax) return 0;
    if (getenv("LIFTLOG")) fprintf(stderr, "[lift] enter %06x%s call#%lu\n", pc, midoff ? " (mid)" : "", lift_calls);
    { static unsigned clo = 1, chi = 0; static int ci; if (!ci) { ci = 1; const char *e = getenv("CHKPC"); if (e) { if (sscanf(e, "%x:%x", &clo, &chi) < 2) chi = clo + 1; lift_chk_max = 100000; } }
      if (lift_check || (pc >= clo && pc < chi && !lift_chk_active)) { int sv = lift_check; lift_check = 1; int rr = lift_try_check(pc, f, midoff, midr0); lift_check = sv; return rr; } }
    lift_sync_in(); uint32_t r0 = midoff ? midr0 : m32(A[7]); lift_synced = 0; lift_calls++; unsigned long calls0 = lift_calls;
    lift_mid = midoff; lift_mid_r0 = midr0;
    volatile int rsp_sv = lift_rsp; lift_rsp = 0; jmp_buf jb_sv; memcpy(jb_sv, lift_jb, sizeof jb_sv);
    if (setjmp(lift_jb) == 0) { f(); lift_bail_pc = lift_ret_pc; }
    lift_rsp = rsp_sv; memcpy(lift_jb, jb_sv, sizeof jb_sv);
    { static unsigned bl, bh; static int bi; if (!bi) { bi = 1; const char *e = getenv("BAILW"); if (e) sscanf(e, "%x:%x", &bl, &bh); } if (bh && lift_bail_pc >= bl && lift_bail_pc < bh) { static int bn; if (bn++ < 5) fprintf(stderr, "[bailw] entry=%06x bail_pc=%06x synced=%d trapbail? A7=%x\n", pc, lift_bail_pc, lift_synced, lift_synced ? R(M68K_REG_A7) : A[7]); } }
    if (!lift_synced) lift_sync_out();
    if (midoff && lift_bail_pc == pc && lift_calls == calls0 && lift_ndead < 64 && !getenv("NOMIDDEAD")) { lift_dead_pcs[lift_ndead++] = pc; fprintf(stderr, "[lift] mid %06x made no progress; disabled\n", pc); }
    W(M68K_REG_PC, lift_bail_pc); return 1;
}

#ifdef LIFT_TRACE
typedef struct { u32 pc, d[8], a[8], f; } TrE; static TrE *trl, *tri_; static int ntrl, ntri; enum { TRMAX = 200000 };
void lift_trace(u32 pc) { if (!lift_dry) return; if (!trl) trl = malloc(sizeof(TrE) * TRMAX); if (ntrl >= TRMAX) return; TrE *e = &trl[ntrl++]; e->pc = pc; memcpy(e->d, D, 32); memcpy(e->a, A, 32); e->f = (fX << 4) | (fN << 3) | (fZ << 2) | (fV << 1) | fC; }
static void tri_record(uint32_t pc) { if (pc >= REL_A5 + 0x20 && pc < REL_A5 + 0xF00) return; if (!tri_) tri_ = malloc(sizeof(TrE) * TRMAX); if (ntri >= TRMAX) return; TrE *e = &tri_[ntri++]; e->pc = pc; for (int i = 0; i < 8; i++) { e->d[i] = R(M68K_REG_D0 + i); e->a[i] = R(M68K_REG_A0 + i); } e->a[7] = SP; e->f = R(M68K_REG_SR) & 0x1F; }
static void tr_compare(void) {
    int n = ntrl < ntri ? ntrl : ntri; int i;
    for (i = 0; i < n; i++) { TrE *x = &trl[i], *y = &tri_[i]; if (x->pc != y->pc || memcmp(x->d, y->d, 32) || memcmp(x->a, y->a, 32) || x->f != y->f) break; }
    if (i < n) { TrE *x = &trl[i], *y = &tri_[i]; fprintf(stderr, "   FIRST DIVERGENCE at step %d: lift pc=%06x interp pc=%06x\n", i, x->pc, y->pc);
        if (i) fprintf(stderr, "   previous instruction at %06x\n", trl[i - 1].pc);
        for (int k = (i > 8 ? i - 8 : 0); k <= i; k++) fprintf(stderr, "     [%d] pc=%06x/%06x A0=%x/%x A1=%x/%x A7=%x/%x D0=%x/%x\n", k, trl[k].pc, tri_[k].pc, trl[k].a[0], tri_[k].a[0], trl[k].a[1], tri_[k].a[1], trl[k].a[7], tri_[k].a[7], trl[k].d[0], tri_[k].d[0]);
        for (int k = 0; k < 8; k++) { if (x->d[k] != y->d[k]) fprintf(stderr, "     D%d lift=%08x interp=%08x\n", k, x->d[k], y->d[k]); if (x->a[k] != y->a[k]) fprintf(stderr, "     A%d lift=%08x interp=%08x\n", k, x->a[k], y->a[k]); }
        if (x->f != y->f) fprintf(stderr, "     flags lift=%02x interp=%02x\n", x->f, y->f); }
    else if (ntrl != ntri) fprintf(stderr, "   trace length differs: lift=%d interp=%d\n", ntrl, ntri);
}
#endif

/* ---- self-check mode (LIFTCHECK=1): run each translated routine on a copy of the state first, then let the
        interpreter execute the same routine and compare registers, flags and all of RAM at the return. ---- */
static int lift_chk_dummy = 0; static uint32_t chk_ret, chk_sp, chk_entry;
static uint8_t *chk_snap; static u32 chk_D[8], chk_A[8], chk_f[5]; /*chk counters declared above*/ static int chk_count[16][16384];
static LiftFn lift_fn_filter; static int lift_fn_budget = -1;
static u32 chk_vis[512]; static int chk_nvis; static u32 chk_s_D[8], chk_s_A[8], chk_s_f[5];
typedef struct { u32 a; u8 v; } ChkDiff; static ChkDiff *chk_diff; static int chk_ndiff, chk_diffcap;
static void chk_save_regs(u32 *d, u32 *a, u32 *f) { memcpy(d, D, 32); memcpy(a, A, 32); f[0] = fX; f[1] = fN; f[2] = fZ; f[3] = fV; f[4] = fC; }
static void chk_load_regs(u32 *d, u32 *a, u32 *f) { memcpy(D, d, 32); memcpy(A, a, 32); fX = f[0]; fN = f[1]; fZ = f[2]; fV = f[3]; fC = f[4]; }
static int lift_try_check(uint32_t pc, LiftFn f, u32 midoff, u32 midr0) {
    int si = 0, off = 0; for (int s = 1; s < 16; s++) if (lift_segtab[s] && pc >= SB[s] && pc < SB[s] + lift_seglen[s]) { si = s; off = (pc - SB[s]) >> 1; }
    if (chk_count[si][off] >= lift_chk_max) return 0;
    lift_sync_in(); uint32_t r0 = midoff ? midr0 : m32(A[7]); chk_save_regs(chk_s_D, chk_s_A, chk_s_f);
    if (!chk_snap) chk_snap = malloc(RAM_SIZE); memcpy(chk_snap, ram, RAM_SIZE);
    extern int lift_dry; lift_dry = 1; lift_nvis = 0;
#ifdef LIFT_TRACE
    ntrl = 0; ntri = 0;
#endif
    lift_rsp = 0; int jr = setjmp(lift_jb); int ok = 0;
    if (jr == 0) { lift_mid = midoff; lift_mid_r0 = midr0; f(); ok = 1; }
    lift_mid = 0;
    lift_dry = 0;
    chk_partial = 0;
    if (!ok && jr != 2 && jr != 3) { memcpy(ram, chk_snap, RAM_SIZE); chk_load_regs(chk_s_D, chk_s_A, chk_s_f); chk_skip++; chk_count[si][off]++; return 0; }
    if (!ok) chk_partial = 1;
    chk_count[si][off]++;
    chk_save_regs(chk_D, chk_A, chk_f); chk_nvis = lift_nvis; memcpy(chk_vis, lift_vis, sizeof(u32) * lift_nvis);
    /* diff of RAM written by the translated run (against the snapshot), then restore */
    chk_ndiff = 0; for (uint32_t i = 0; i < RAM_SIZE; i += 8) if (*(uint64_t *)(ram + i) != *(uint64_t *)(chk_snap + i)) for (int k = 0; k < 8; k++) if (ram[i + k] != chk_snap[i + k]) {
        if (chk_ndiff == chk_diffcap) { chk_diffcap = chk_diffcap ? chk_diffcap * 2 : 1024; chk_diff = realloc(chk_diff, chk_diffcap * sizeof(ChkDiff)); }
        chk_diff[chk_ndiff].a = i + k; chk_diff[chk_ndiff].v = ram[i + k]; chk_ndiff++; ram[i + k] = chk_snap[i + k]; }
    chk_load_regs(chk_s_D, chk_s_A, chk_s_f);
    if (chk_partial) { chk_ret = lift_trap_pc; chk_sp = chk_A[7]; chk_part_n++; } else { chk_ret = lift_ret_pc; chk_sp = chk_A[7]; }
    chk_entry = pc; lift_chk_active = 1; lift_on = 0;      /* interpreter runs the routine; compare at its return */
    if (chk_partial && chk_ret == pc && chk_sp == SP) lift_check_finish();
#ifdef LIFT_TRACE
    tri_record(pc);
#endif
    return 0;
}
static void lift_check_finish(void) {
    lift_chk_active = 0; lift_on = 1; int bad = 0; char msg[2048]; int ml = 0;
    uint32_t er[8], ea_[8]; for (int i = 0; i < 8; i++) { er[i] = R(M68K_REG_D0 + i); ea_[i] = R(M68K_REG_A0 + i); }
    ea_[7] = SP; uint32_t sr = R(M68K_REG_SR);
    for (int i = 0; i < 8; i++) { if (er[i] != chk_D[i]) { bad = 1; ml += snprintf(msg + ml, 200, " D%d interp=%08x lift=%08x;", i, er[i], chk_D[i]); } if (ea_[i] != chk_A[i]) { bad = 1; ml += snprintf(msg + ml, 200, " A%d interp=%08x lift=%08x;", i, ea_[i], chk_A[i]); } }
    u32 ef[5] = {(sr >> 4) & 1, (sr >> 3) & 1, (sr >> 2) & 1, (sr >> 1) & 1, sr & 1}; const char *fn_[5] = {"X", "N", "Z", "V", "C"};
    for (int i = 0; i < 5; i++) if (ef[i] != chk_f[i]) { bad = 1; ml += snprintf(msg + ml, 100, " flag%s interp=%u lift=%u;", fn_[i], ef[i], chk_f[i]); }
    for (int i = 0; i < chk_ndiff; i++) chk_snap[chk_diff[i].a] = chk_diff[i].v;              /* expected RAM */
    int nb = 0; for (uint32_t i = 0; i < RAM_SIZE && nb < 6; i += 8) if (*(uint64_t *)(ram + i) != *(uint64_t *)(chk_snap + i)) for (int k = 0; k < 8 && nb < 6; k++) if (ram[i + k] != chk_snap[i + k]) {
        bad = 1; nb++; ml += snprintf(msg + ml, 100, " ram[%06x] interp=%02x lift=%02x;", i + k, ram[i + k], chk_snap[i + k]); }
    if (bad) { chk_bad++; int s = 0; for (int q = 1; q < 16; q++) if (lift_segtab[q] && chk_entry >= SB[q] && chk_entry < SB[q] + lift_seglen[q]) s = q;
        fprintf(stderr, "[LIFTCHECK] MISMATCH in routine seg%d+0x%x (pc=%06x):%s\n   nested calls:", s, chk_entry - SB[s], chk_entry, msg); for (int q = 0; q < chk_nvis; q++) { int sq = 0; for (int z = 1; z < 16; z++) if (lift_segtab[z] && chk_vis[q] >= SB[z] && chk_vis[q] < SB[z] + lift_seglen[z]) sq = z; fprintf(stderr, " %d+%x", sq, chk_vis[q] - SB[sq]); } fprintf(stderr, "\n");
#ifdef LIFT_TRACE
        tr_compare();
#endif
    }
    else { chk_ok++; if (getenv("CHKPC")) fprintf(stderr, "[chkpc] finish OK entry=%06x partial-ret=%06x partial=%d\n", chk_entry, chk_ret, chk_partial); }
}

