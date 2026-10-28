/* ---- glue between translated code (gen/lift_*.c) and the interpreter / HLE ---- */
static void hle(uint16_t w, uint32_t ret);
u32 D[8], A[8], SB[16], REL_A4, REL_A5; u32 fX, fN, fZ, fV, fC;
static jmp_buf lift_jb; static u32 lift_bail_pc; static int lift_synced, lift_active, lift_on = 1;
static LiftFn *lift_segtab[16]; static u32 lift_seglen[16]; static u32 lift_lo = 0xFFFFFFFF, lift_hi = 0;
int lift_dry = 0; static unsigned long lift_calls, lift_bails, lift_traps;

static void lift_sync_in(void) {
    for (int i = 0; i < 8; i++) { D[i] = R(M68K_REG_D0 + i); A[i] = R(M68K_REG_A0 + i); }
    A[7] = SP; uint32_t sr = R(M68K_REG_SR); fX = (sr >> 4) & 1; fN = (sr >> 3) & 1; fZ = (sr >> 2) & 1; fV = (sr >> 1) & 1; fC = sr & 1;
}
static void lift_sync_out(void) {
    for (int i = 0; i < 7; i++) { W(M68K_REG_D0 + i, D[i]); W(M68K_REG_A0 + i, A[i]); }
    W(M68K_REG_D7, D[7]); setsp(A[7]);
    uint32_t sr = R(M68K_REG_SR) & ~0x1Fu; sr |= (fX << 4) | (fN << 3) | (fZ << 2) | (fV << 1) | fC; W(M68K_REG_SR, sr);
}
static void lift_register_seg(int segno, uint32_t base, uint32_t len) {
    if (segno < 0 || segno > 15 || !lift_nents) return;
    SB[segno] = base; lift_seglen[segno] = len; free(lift_segtab[segno]); lift_segtab[segno] = calloc(len / 2 + 2, sizeof(LiftFn));
    for (int i = 0; i < lift_nents; i++) if (lift_ents[i].seg == segno && lift_ents[i].off < len) lift_segtab[segno][lift_ents[i].off >> 1] = lift_ents[i].fn;
    if (base < lift_lo) lift_lo = base; if (base + len > lift_hi) lift_hi = base + len;
    if (!REL_A4) { REL_A4 = R(M68K_REG_A4); REL_A5 = R(M68K_REG_A5); fprintf(stderr, "[lift] REL_A4=%x REL_A5=%x\n", REL_A4, REL_A5); }
}
static LiftFn lift_lookup(uint32_t pc) {
    if (pc < lift_lo || pc >= lift_hi) return NULL;
    for (int s = 1; s < 16; s++) if (lift_segtab[s] && pc >= SB[s] && pc < SB[s] + lift_seglen[s] && !(pc & 1)) return lift_segtab[s][(pc - SB[s]) >> 1];
    return NULL;
}
void lift_bail(u32 pc) { if (lift_dry) longjmp(lift_jb, 3); lift_bail_pc = pc; lift_bails++; longjmp(lift_jb, 1); }
void lift_ret_mismatch(u32 r) { lift_bail(r); }
static u32 lift_vis[512]; static int lift_nvis;
void lift_note(LiftFn f) { if (lift_nvis >= 512) return; for (int i = 0; i < lift_nents; i++) if (lift_ents[i].fn == f) { lift_vis[lift_nvis++] = SB[lift_ents[i].seg] + lift_ents[i].off; return; } }
void lift_enter(u32 tgt) {
    for (int k = 0; k < 4 && m16(tgt) == 0x4EF9; k++) tgt = m32(tgt + 2);      /* follow jump-table stubs */
    LiftFn f = lift_lookup(tgt);
    if (lift_dry && lift_nvis < 512) lift_vis[lift_nvis++] = tgt;
    if (f) { lift_calls++; f(); } else lift_bail(tgt);
}
void lift_trap(u32 w, u32 tpc, u32 ret) {
    if (lift_dry) longjmp(lift_jb, 2);
    if (patched[trap_key(w, 0)]) { A[7] -= 4; w32(A[7], ret); lift_enter(patched[trap_key(w, 0)]); return; }
    lift_traps++; lift_sync_out(); trap_pc = tpc; trap_from_stub = 0; wm_redirected = 0;
    hle((uint16_t)w, ret);
    if (!(w & 0x800) && !halted) { uint32_t d0 = R(M68K_REG_D0) & 0xFFFF; uint32_t sr = R(M68K_REG_SR) & ~0xFu; if (d0 == 0) sr |= 4; if (d0 & 0x8000) sr |= 8; W(M68K_REG_SR, sr); }
    uint32_t pc2 = R(M68K_REG_PC);
    if (pc2 != ret || halted) { lift_synced = 1; lift_bail_pc = pc2; lift_bails++; longjmp(lift_jb, 1); }
    lift_sync_in();
}
static int lift_try_check(uint32_t pc, LiftFn f); static int lift_check = 0, lift_chk_active;
/* called from the instruction hook when pc is a translated routine's entry; returns 1 if it ran */
static int lift_try(uint32_t pc) {
    LiftFn f = lift_lookup(pc); if (!f || !lift_on) return 0;
    if (lift_check) return lift_try_check(pc, f);
    lift_sync_in(); uint32_t r0 = m32(A[7]); lift_synced = 0; lift_calls++;
    if (setjmp(lift_jb) == 0) { f(); lift_bail_pc = r0; }
    if (!lift_synced) lift_sync_out();
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
static int lift_chk_dummy = 0, lift_chk_max = 5; static uint32_t chk_ret, chk_sp, chk_entry;
static uint8_t *chk_snap; static u32 chk_D[8], chk_A[8], chk_f[5]; static int chk_ok = 0, chk_bad = 0, chk_skip = 0; static int chk_count[16][16384];
static LiftFn lift_fn_filter; static int lift_fn_budget = -1;
static u32 chk_vis[512]; static int chk_nvis; static u32 chk_s_D[8], chk_s_A[8], chk_s_f[5];
typedef struct { u32 a; u8 v; } ChkDiff; static ChkDiff *chk_diff; static int chk_ndiff, chk_diffcap;
static void chk_save_regs(u32 *d, u32 *a, u32 *f) { memcpy(d, D, 32); memcpy(a, A, 32); f[0] = fX; f[1] = fN; f[2] = fZ; f[3] = fV; f[4] = fC; }
static void chk_load_regs(u32 *d, u32 *a, u32 *f) { memcpy(D, d, 32); memcpy(A, a, 32); fX = f[0]; fN = f[1]; fZ = f[2]; fV = f[3]; fC = f[4]; }
static int lift_try_check(uint32_t pc, LiftFn f) {
    int si = 0, off = 0; for (int s = 1; s < 16; s++) if (lift_segtab[s] && pc >= SB[s] && pc < SB[s] + lift_seglen[s]) { si = s; off = (pc - SB[s]) >> 1; }
    if (chk_count[si][off] >= lift_chk_max) return 0;
    lift_sync_in(); uint32_t r0 = m32(A[7]); chk_save_regs(chk_s_D, chk_s_A, chk_s_f);
    if (!chk_snap) chk_snap = malloc(RAM_SIZE); memcpy(chk_snap, ram, RAM_SIZE);
    extern int lift_dry; lift_dry = 1; lift_nvis = 0;
#ifdef LIFT_TRACE
    ntrl = 0; ntri = 0;
#endif
    int jr = setjmp(lift_jb); int ok = 0;
    if (jr == 0) { f(); ok = 1; }
    lift_dry = 0;
    if (!ok) { memcpy(ram, chk_snap, RAM_SIZE); chk_load_regs(chk_s_D, chk_s_A, chk_s_f); chk_skip++; chk_count[si][off]++; return 0; }
    chk_count[si][off]++;
    chk_save_regs(chk_D, chk_A, chk_f); chk_nvis = lift_nvis; memcpy(chk_vis, lift_vis, sizeof(u32) * lift_nvis);
    /* diff of RAM written by the translated run (against the snapshot), then restore */
    chk_ndiff = 0; for (uint32_t i = 0; i < RAM_SIZE; i += 8) if (*(uint64_t *)(ram + i) != *(uint64_t *)(chk_snap + i)) for (int k = 0; k < 8; k++) if (ram[i + k] != chk_snap[i + k]) {
        if (chk_ndiff == chk_diffcap) { chk_diffcap = chk_diffcap ? chk_diffcap * 2 : 1024; chk_diff = realloc(chk_diff, chk_diffcap * sizeof(ChkDiff)); }
        chk_diff[chk_ndiff].a = i + k; chk_diff[chk_ndiff].v = ram[i + k]; chk_ndiff++; ram[i + k] = chk_snap[i + k]; }
    chk_load_regs(chk_s_D, chk_s_A, chk_s_f);
    chk_ret = r0; chk_sp = A[7] + 4; chk_entry = pc; lift_chk_active = 1; lift_on = 0;      /* interpreter runs the routine; compare at its return */
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
    else chk_ok++;
}

