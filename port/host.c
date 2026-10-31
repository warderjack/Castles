/* Castles: Siege and Conquest -- 68k host with Toolbox HLE (milestone 1: headless boot trace) */
#include <sys/stat.h>
unsigned char lift_fcov[16][1 << 15];
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m68k.h"
/* every getenv() in this program takes a string literal and the environment never changes: resolve each call site once */
#define getenv(s) ({ static const char *gc_; static char gi_; if (!gi_) { gc_ = (getenv)(s); gi_ = 1; } gc_; })

#define RAM_SIZE   0x1000000u
#define MP_BASE    0x004000u            /* master pointer table */
#define MP_COUNT   0x10000u
#define HEAP_START 0x050000u
#ifndef HEAP_END
#define HEAP_END   0x0B00000u
#endif
#define STACK_TOP  0x0C00000u           /* == CurStackBase (bottom of A5 world) */
#define STUB_BASE  0x0F00000u           /* trap stubs: 4 bytes per key */
#define STUB_END   (STUB_BASE + 0x1000u * 4)
#define GCONT_PC   (STUB_BASE + 0x5008u)        /* guest-call continuation: proc returns here */
#define YIELD_PC   (STUB_BASE + 0x5010u)        /* two NOPs: rewound traps resume at +2 after the slice ends */
static uint32_t yield_resume = 0;

uint8_t ram[RAM_SIZE];
#include "liftrt.h"
static void lift_register_seg(int segno, uint32_t base, uint32_t len);

static inline uint32_t rd8(uint32_t a)  { return ram[a & 0xFFFFFF]; }
static inline uint32_t rd16(uint32_t a) { a &= 0xFFFFFF; return (ram[a] << 8) | ram[a + 1]; }
static inline uint32_t rd32(uint32_t a) { a &= 0xFFFFFF; return ((uint32_t)ram[a] << 24) | (ram[a + 1] << 16) | (ram[a + 2] << 8) | ram[a + 3]; }
static inline void wr8(uint32_t a, uint32_t v)  { ram[a & 0xFFFFFF] = v; }
static inline void wr16(uint32_t a, uint32_t v) { a &= 0xFFFFFF; ram[a] = v >> 8; ram[a + 1] = v; }
static inline void wr32(uint32_t a, uint32_t v) { a &= 0xFFFFFF; ram[a] = v >> 24; ram[a + 1] = v >> 16; ram[a + 2] = v >> 8; ram[a + 3] = v; }

unsigned int m68k_read_memory_8(unsigned int a)  { return rd8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return rd16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return rd32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { wr8(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { wr16(a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { wr32(a, v); }

static uint32_t R(int r) { return m68k_get_reg(NULL, (m68k_register_t)r); }
static void W(int r, uint32_t v) { m68k_set_reg((m68k_register_t)r, v); }
#define D(n) R(M68K_REG_D0 + (n))
#define A(n) R(M68K_REG_A0 + (n))
#define SETD(n, v) W(M68K_REG_D0 + (n), (v))
#define SETA(n, v) W(M68K_REG_A0 + (n), (v))
#define SP R(M68K_REG_SP)
static void setsp(uint32_t v) { W(M68K_REG_SP, v); }
static void push32(uint32_t v) { uint32_t s = SP - 4; setsp(s); wr32(s, v); }
static uint32_t pop32(void) { uint32_t s = SP; uint32_t v = rd32(s); setsp(s + 4); return v; }

static int halted = 0; static int chk_slices = 0;
static void die(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
static void die(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); fprintf(stderr, "HALT: "); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n"); va_end(ap);
    halted = 1; m68k_end_timeslice();
}

/* ---------------- Resource files ---------------- */
typedef struct { uint32_t type; int16_t id; uint8_t attrs; uint32_t off, len; uint32_t handle; char name[64]; const uint8_t *buf; int file; int dead; int dirty; } Res;
typedef struct { int used; char path[1100]; uint8_t *buf; size_t len; int changed; } RFile;
static RFile rfiles[32]; static int chain[32], nchain = 0, cur_rf = 0;
static uint8_t *rfork; static size_t forklen;
static Res res[8192]; static int nres;

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint32_t be16(const uint8_t *p) { return (p[0] << 8) | p[1]; }

static void parse_rsrc(const uint8_t *fk, size_t len, int fi) {
    uint32_t dataoff = be32(fk), mapoff = be32(fk + 4);
    const uint8_t *map = fk + mapoff;
    uint32_t tl = be16(map + 24);
    const uint8_t *tlist = map + tl;
    int ntypes = (int)be16(tlist) + 1;
    for (int t = 0; t < ntypes; t++) {
        const uint8_t *te = tlist + 2 + t * 8;
        int cnt = (int)be16(te + 4) + 1; uint32_t refoff = be16(te + 6);
        for (int i = 0; i < cnt && nres < 8192; i++) {
            const uint8_t *re = tlist + refoff + i * 12;
            Res *r = &res[nres++]; memset(r, 0, sizeof *r);
            r->type = be32(te); r->id = (int16_t)be16(re); r->attrs = re[4];
            uint32_t doff = ((uint32_t)re[5] << 16) | (re[6] << 8) | re[7];
            r->off = dataoff + doff + 4; r->len = be32(fk + dataoff + doff); r->buf = fk; r->file = fi;
            { uint32_t no = be16(re + 2); if (no != 0xFFFF) { const uint8_t *np = map + be16(map + 26) + no; int nl = np[0]; if (nl > 63) nl = 63; memcpy(r->name, np + 1, nl); r->name[nl] = 0; } }
        }
    }
}
static char game_root[1024] = ".";
static void load_fork(const char *path) {
    char alt[1200]; struct stat sb; size_t pl = strlen(path); long skip = 0; int isbin = pl > 4 && !strcasecmp(path + pl - 4, ".bin");
    if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) {      /* game folder: find the application's MacBinary file */
        static const char *names[] = {"Castles - Siege & Conquest.bin", "Castles - Siege and Conquest.bin", "Castles.bin", NULL}; int ok = 0;
        for (int i = 0; names[i] && !ok; i++) { snprintf(alt, sizeof alt, "%s/%s", path, names[i]); ok = stat(alt, &sb) == 0; }
        if (!ok) { fprintf(stderr, "no application .bin found in %s\n", path); exit(1); }
        strncpy(game_root, path, sizeof game_root - 1); path = alt; isbin = 1;
    }
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END); forklen = ftell(f); fseek(f, 0, SEEK_SET);
    if (isbin) { uint8_t hd[128]; if (fread(hd, 1, 128, f) != 128) exit(1); uint32_t dl = be32(hd + 83), rl = be32(hd + 87); skip = 128 + ((dl + 127) & ~127u); forklen = rl; fseek(f, skip, SEEK_SET); }
    rfork = malloc(forklen); if (fread(rfork, 1, forklen, f) != forklen) exit(1); fclose(f);
    rfiles[0].used = 1; rfiles[0].buf = rfork; rfiles[0].len = forklen; chain[nchain++] = 0; cur_rf = 0;
    parse_rsrc(rfork, forklen, 0);
    fprintf(stderr, "resources: %d\n", nres);
}
static Res *find_res(uint32_t type, int id) { for (int c = nchain - 1; c >= 0; c--) for (int i = 0; i < nres; i++) if (res[i].file == chain[c] && !res[i].dead && res[i].type == type && res[i].id == id) return &res[i]; return NULL; }
static Res *find_res_1(uint32_t type, int id) { for (int i = 0; i < nres; i++) if (res[i].file == cur_rf && !res[i].dead && res[i].type == type && res[i].id == id) return &res[i]; return NULL; }
static Res *res_from_handle(uint32_t h) { if (!h) return NULL; for (int i = 0; i < nres; i++) if (res[i].handle == h && !res[i].dead) return &res[i]; return NULL; }

/* ---------------- Memory manager: first/best-fit heap with coalescing free list; handles never move ---------------- */
static uint32_t next_slot = 0;
static uint32_t hsize[MP_COUNT]; static uint8_t hflags[MP_COUNT];
static uint32_t *free_slots; static int nfree_slots = 0, capfree_slots = 0;
typedef struct { uint32_t a, n; } Ext;               /* a = block start (header), n = total bytes incl. 8-byte header */
static Ext *fl; static int nfl = 0, capfl = 0; static int heap_ready = 0; static uint32_t heap_free = 0;
static void fl_insert(uint32_t a, uint32_t n) {       /* insert sorted, coalescing */
    int lo = 0, hi = nfl; while (lo < hi) { int m = (lo + hi) / 2; if (fl[m].a < a) lo = m + 1; else hi = m; }
    if (lo > 0 && fl[lo - 1].a + fl[lo - 1].n == a) { fl[lo - 1].n += n; if (lo < nfl && fl[lo - 1].a + fl[lo - 1].n == fl[lo].a) { fl[lo - 1].n += fl[lo].n; memmove(&fl[lo], &fl[lo + 1], (nfl - lo - 1) * sizeof(Ext)); nfl--; } return; }
    if (lo < nfl && a + n == fl[lo].a) { fl[lo].a = a; fl[lo].n += n; return; }
    if (nfl == capfl) { capfl = capfl ? capfl * 2 : 256; fl = realloc(fl, capfl * sizeof(Ext)); }
    memmove(&fl[lo + 1], &fl[lo], (nfl - lo) * sizeof(Ext)); fl[lo].a = a; fl[lo].n = n; nfl++;
}
#define FB_RESERVE_END 0x00A50000u      /* framebuffer (FB_BASE=0xA00000, 640x480 bytes) lives inside the heap range: never hand it out */
static void heap_init(void) { heap_ready = 1;
    if (HEAP_END > 0x00A00000u && HEAP_START < 0x00A00000u) { fl_insert(HEAP_START, 0x00A00000u - HEAP_START); heap_free = 0x00A00000u - HEAP_START; if (HEAP_END > FB_RESERVE_END) { fl_insert(FB_RESERVE_END, HEAP_END - FB_RESERVE_END); heap_free += HEAP_END - FB_RESERVE_END; } }
    else { fl_insert(HEAP_START, HEAP_END - HEAP_START); heap_free = HEAP_END - HEAP_START; } }
static uint32_t heap_maxblock(void) { uint32_t m = 0; for (int i = 0; i < nfl; i++) if (fl[i].n > m) m = fl[i].n; return m > 8 ? m - 8 : 0; }
static int purge_for(uint32_t need); static int purging;
static struct { uint32_t p, n, pc; } hl_live[1 << 16]; static int hl_on = -1; static uint32_t trap_pc, hl_ret;
static void hl_add(uint32_t p, uint32_t n) { if (hl_on < 0) hl_on = getenv("HEAPLOG") != NULL; if (!hl_on) return; for (int i = 0; i < (1 << 16); i++) { int k = (int)(((p >> 3) + i) & 0xFFFF); if (!hl_live[k].p) { hl_live[k].p = p; hl_live[k].n = n; hl_live[k].pc = hl_ret; return; } } }
static void hl_del(uint32_t p) { if (!hl_on) return; for (int i = 0; i < (1 << 16); i++) { int k = (int)(((p >> 3) + i) & 0xFFFF); if (hl_live[k].p == p) { hl_live[k].p = 0xFFFFFFFF; return; } if (!hl_live[k].p) return; } }
static uint32_t alloc_block(uint32_t n) {
    if (!heap_ready) heap_init();
    n = (n + 3) & ~3u; if (n == 0) n = 4;
    uint32_t need = n + 8; int best = -1; int purged_once = 0;
retry:
    best = -1;
    for (int i = 0; i < nfl; i++) if (fl[i].n >= need && (best < 0 || fl[i].n < fl[best].n)) { best = i; if (fl[i].n == need) break; }
    if (best < 0 && !purged_once && !purging) { purged_once = 1; if (purge_for(need)) goto retry; }
    if (best < 0) { fprintf(stderr, "[heap] out of memory (n=%u free=%u max=%u)\n", n, heap_free, heap_maxblock()); return 0; }
    uint32_t b = fl[best].a, tot = fl[best].n;
    if (tot - need >= 24) { fl[best].a += need; fl[best].n -= need; tot = need; }
    else { memmove(&fl[best], &fl[best + 1], (nfl - best - 1) * sizeof(Ext)); nfl--; }
    heap_free -= tot; wr32(b, tot - 8); wr32(b + 4, 1); memset(ram + b + 8, 0, tot - 8);
    hl_add(b + 8, tot - 8); return b + 8;
}
static void free_block(uint32_t p) {
    if (!p) return; uint32_t b = p - 8; if (b < HEAP_START || b >= HEAP_END || rd32(b + 4) != 1) return;
    hl_del(p); uint32_t tot = rd32(b) + 8; wr32(b + 4, 0); heap_free += tot; fl_insert(b, tot);
}
static uint32_t new_handle(uint32_t n) {
    uint32_t s;
    if (nfree_slots) s = free_slots[--nfree_slots]; else { if (next_slot >= MP_COUNT) { die("out of master pointers"); return 0; } s = next_slot++; }
    uint32_t p = alloc_block(n); if (!p) { if (s == next_slot - 1) next_slot--; else free_slots[nfree_slots++] = s; return 0; }
    wr32(MP_BASE + s * 4, p); hsize[s] = n; hflags[s] = 0;
    return MP_BASE + s * 4;
}
static uint32_t slot_of(uint32_t h) { return (h - MP_BASE) / 4; }
static void dispose_handle(uint32_t h) {
    if (h < MP_BASE || h >= MP_BASE + MP_COUNT * 4) return; uint32_t s = slot_of(h); if (s >= next_slot) return;
    uint32_t p = rd32(h); if (!p) { return; }
    free_block(p); wr32(h, 0); hsize[s] = 0;
    if (nfree_slots == capfree_slots) { capfree_slots = capfree_slots ? capfree_slots * 2 : 256; free_slots = realloc(free_slots, capfree_slots * 4); }
    free_slots[nfree_slots++] = s;
}

static uint32_t res_get_handle(Res *r) {
    if (!r->handle) {
        r->handle = new_handle(r->len); if (!r->handle) return 0;
        if (r->buf) memcpy(ram + rd32(r->handle), r->buf + r->off, r->len); else memset(ram + rd32(r->handle), 0, r->len);
        hflags[slot_of(r->handle)] = 0x20 | ((r->attrs & 0x20) ? 0x40 : 0) | ((r->attrs & 0x10) ? 0x80 : 0);
    } else if (!rd32(r->handle) && r->buf) {        /* purged: reload into the same master pointer */
        uint32_t p = alloc_block(r->len); if (p) { wr32(r->handle, p); memcpy(ram + p, r->buf + r->off, r->len); }
    }
    return r->handle;
}
/* Memory Manager purge: free unlocked purgeable file-backed resources (the original app heap was small; the Mac purged these on demand) */
static int purge_for(uint32_t need) {
    int any = 0; purging = 1;
    for (int i = 0; i < nres; i++) {
        Res *r = &res[i]; if (r->dead || !r->handle || !r->buf || r->dirty) continue;
        uint32_t s = slot_of(r->handle); if (s >= next_slot) continue; uint32_t p = rd32(r->handle);
        if (!p || !(hflags[s] & 0x40) || (hflags[s] & 0x80)) continue;
        free_block(p); wr32(r->handle, 0); any = 1; if (heap_maxblock() >= need + 64) break;
    }
    purging = 0; if (any && getenv("HEAPLOG")) fprintf(stderr, "[heap] purged resources (free=%u max=%u)\n", heap_free, heap_maxblock());
    return any;
}

/* ---------------- Trap infrastructure ---------------- */
static uint32_t patched[0x1000];
static int trap_key(uint32_t w, int newtool) {
    if ((w & 0x800) || newtool) return 0x400 + (w & 0x3FF);
    return w & 0xFF;
}
#include "trapnames.h"
#define tname tname_tbl

static uint32_t pcring[64]; static int pcri = 0;
static unsigned long ntraps;
static unsigned long thist[65536]; static struct { uint16_t w; uint32_t d0, a0, a1, sp, ret; } tring[600]; static int tri = 0;
static int trace_limit = 150; static int trace_init_done;

static void do_loadseg(uint32_t ret) {
    uint32_t segno = rd16(SP); setsp(SP + 2);
    Res *r = find_res(0x434F4445 /*CODE*/, segno);
    if (!r) { die("LoadSeg: no CODE %u", segno); return; }
    uint32_t h = res_get_handle(r), p = rd32(h);
    uint32_t first = rd16(p), count = rd16(p + 2);
    uint32_t a5 = R(M68K_REG_A5), jt = a5 + 0x20;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t e = jt + first + i * 8, off = rd16(e);
        wr16(e, segno); wr16(e + 2, 0x4EF9); wr32(e + 4, p + 4 + off);
    }
    fprintf(stderr, "  [seg] LoadSeg(%u) base=%06x\n", segno, p); lift_register_seg(segno, p, r->len); if (trace_limit > 0) fprintf(stderr, "  LoadSeg(%u) first=%u count=%u base=%06x -> jump %06x\n", segno, first, count, p, ret - 6);
    W(M68K_REG_PC, ret - 6);   /* stub entry + 2, now a JMP.L */
}

/* Returns via W(PC) -- handler may redirect; otherwise PC=ret. */
#include <time.h>
#include <strings.h>
static int det; static unsigned long det_stop, det_every, det_ymax; static unsigned long det_slices, det_next = 128; static int det_yield;
static uint32_t ticks_now(void) { if (det) return (uint32_t)(180 + det_slices / 25); static uint64_t t0; struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); uint64_t ms = t.tv_sec * 1000ull + t.tv_nsec / 1000000; if (!t0) t0 = ms; static int sc; if (!sc) { const char *e = getenv("CASTLES_TICKSCALE"); sc = e ? atoi(e) : 1; if (sc < 1) sc = 1; } return (uint32_t)((ms - t0) * sc * 60 / 1000 + 180); }
static int16_t res_err = 0;
#define FB_BASE 0x00A00000u
static uint32_t cur_a5(void) { return R(M68K_REG_A5); }

static uint32_t set_handle_size(uint32_t h, uint32_t n) {
    uint32_t s = slot_of(h);
    uint32_t old = rd32(h), cap = old ? rd32(old - 8) : 0;
    if (n > cap) { uint32_t nb = alloc_block(n); if (!nb) return (uint32_t)-108; if (old) { memcpy(ram + nb, ram + old, hsize[s] < n ? hsize[s] : n); free_block(old); } wr32(h, nb); }
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
#include "fs.inc"
#include "rm.inc"
#include "qd.inc"
#include "gfx.inc"
#include "gfxd.inc"
#include "pict.inc"
#include "plat.inc"
#include "wm.inc"
#include "wm2.inc"
#include "wm3.inc"
#include "wm4.inc"
#include "dlg.inc"
#include "pal.inc"
#include "sane.inc"
#include "snd.inc"
#include "tasks.inc"
#include "sfg.inc"
#include "movie.inc"
#include "lift_host.inc"
static void hle(uint16_t w, uint32_t ret) {
    hl_ret = ret;
    ntraps++;
    int redirected = 0;
    uint16_t n = (w & 0x800) ? (w & 0xFBFF) : (w & 0xF8FF);   /* normalised trap word */
    thist[w]++; if (!trace_init_done) { trace_init_done = 1; if (getenv("TRACEN")) trace_limit = atoi(getenv("TRACEN")); }
    { int ri_ = tri++ % 600; tring[ri_].w = w; tring[ri_].d0 = D(0); tring[ri_].a0 = A(0); tring[ri_].a1 = A(1); tring[ri_].sp = SP; tring[ri_].ret = ret; }
    { static uint32_t tf; static int tfi; if (!tfi) { tfi = 1; tf = getenv("TRACE_FROM") ? atoi(getenv("TRACE_FROM")) : 0; } if (tf && ticks_now() >= tf) { tf = 0; trace_limit = getenv("TRACE_COUNT") ? atoi(getenv("TRACE_COUNT")) : 2000; } }
    if (trace_limit > 0) {
        trace_limit--;
        fprintf(stderr, "[trap %04X %-18s] D0=%08x A0=%08x A1=%08x SP=%06x ret=%06x\n", w, tname(w), D(0), A(0), A(1), SP, ret);
    }
    static int wmt_on = -1; if (wmt_on < 0) wmt_on = getenv("WMT") != NULL;
    if (wmt_on && ((n >= 0xA900 && n <= 0xA93C && n != 0xA92C && n != 0xA924) || n == 0xAA45 || n == 0xA97C || n == 0xA982 || n == 0xA983 || n == 0xA873 || n == 0xA922 || n == 0xA923)) fprintf(stderr, "[wmt] %04X %s sp0=%08x sp4=%08x sp8=%08x ret=%06x tick=%u\n", w, tname(w), rd32(SP), rd32(SP + 4), rd32(SP + 8), ret, ticks_now());
    uint32_t sp = SP;
    if (rm_dispatch(w, n, ret)) { W(M68K_REG_PC, ret); return; }
    if (dlg_dispatch(w, n, ret)) { if (!wm_redirected) W(M68K_REG_PC, ret); wm_redirected = 0; return; }
    if (sf_dispatch(w, n, ret)) { if (!wm_redirected) W(M68K_REG_PC, ret); wm_redirected = 0; return; }
    if (wm_dispatch(w, n, ret)) { if (!wm_redirected) W(M68K_REG_PC, ret); wm_redirected = 0; return; }
    switch (n) {
    /* ---- Memory Manager ---- */
    case 0xA01E: { uint32_t p = alloc_block(D(0)); SETA(0, p); SETD(0, p ? 0 : (uint32_t)-108); break; }
    case 0xA022: { uint32_t h = new_handle(D(0)); SETA(0, h); SETD(0, h ? 0 : (uint32_t)-108); break; }
    case 0xA066: SETA(0, new_handle(0)); SETD(0, 0); break;        /* NewEmptyHandle */
    case 0xA01F: free_block(A(0)); SETD(0, 0); break;              /* DisposPtr */
    case 0xA023: dispose_handle(A(0)); SETD(0, 0); break;          /* DisposHandle */
    case 0xA020: SETD(0, 0); break;                                /* SetPtrSize (no-op) */
    case 0xA021: SETD(0, rd32(A(0) - 8)); break;                   /* GetPtrSize */
    case 0xA024: SETD(0, set_handle_size(A(0), D(0))); break;      /* SetHandleSize */
    case 0xA025: SETD(0, hsize[slot_of(A(0))]); break;             /* GetHandleSize */
    case 0xA027: SETD(0, set_handle_size(A(0), D(0))); break;      /* ReallocHandle */
    case 0xA029: hflags[slot_of(A(0))] |= 0x80; SETD(0, 0); break;
    case 0xA02A: hflags[slot_of(A(0))] &= ~0x80; SETD(0, 0); break;
    case 0xA049: hflags[slot_of(A(0))] |= 0x40; SETD(0, 0); break;                  /* HPurge */
    case 0xA04A: hflags[slot_of(A(0))] &= ~0x40; SETD(0, 0); break;                 /* HNoPurge */
    case 0xA02B: case 0xA064: case 0xA067: case 0xA068: SETD(0, 0); break; /* HPurge,HNoPurge,EmptyHandle,MoveHHi,HSet/ClrRBit */
    case 0xA069: SETD(0, hflags[slot_of(A(0))]); break;
    case 0xA06A: hflags[slot_of(A(0))] = D(0) & 0xFF; SETD(0, 0); break;
    case 0xA036: case 0xA063: case 0xA02D: case 0xA04B: case 0xA040: case 0xA04D: SETD(0, 0); break; /* MoreMasters,MaxApplZone,SetApplLimit,SetGrowZone,ResrvMem,PurgeMem */
    case 0xA01C: SETD(0, heap_free); break;                                       /* FreeMem */
    case 0xA04C: case 0xA061: SETD(0, heap_maxblock()); break;                    /* CompactMem, MaxBlock */
    case 0xA01D: SETD(0, heap_maxblock()); SETA(0, 0); break;                     /* MaxMem */
    case 0xA062: SETD(0, heap_free); SETA(0, heap_maxblock()); break;            /* PurgeSpace */
    case 0xA065: SETD(0, SP - rd32(0x130)); break;                                 /* StackSpace */
    case 0xA055: break;                                                            /* StripAddress */
    case 0xA028: { uint32_t p = A(0), hh = 0; for (uint32_t s = 0; s < next_slot; s++) if (rd32(MP_BASE + s * 4) == p) { hh = MP_BASE + s * 4; break; } SETA(0, hh); SETD(0, hh ? 0 : (uint32_t)-111); break; }  /* RecoverHandle */
    case 0xA01A: SETA(0, HEAP_START); SETD(0, 0); break;                           /* GetZone */
    case 0xA01B: SETD(0, 0); break;                                                /* SetZone */
    case 0xA026: SETA(0, HEAP_START); SETD(0, 0); break;                           /* HandleZone */
    case 0xA019: case 0xA02C: SETD(0, 0); break;                                   /* InitZone / InitApplZone */
    case 0xA039: wr32(0x20C, (uint32_t)((det ? 3900000000u - 2082844800u : (uint32_t)time(NULL)) + 2082844800u)); SETD(0, 0); break; /* ReadDateTime */
    case 0xA03B: SETD(0, 0); break;                                                /* Delay (handled loosely) */
    case 0xA013: case 0xA045: case 0xA017: case 0xA035: SETD(0, 0); break;                  /* FlushVol, FlushFile, Eject, Offline */
    case 0xA02E: case 0xA22E: { memmove(ram + (A(1) & 0xFFFFFF), ram + (A(0) & 0xFFFFFF), D(0)); SETD(0, 0); break; }
    case 0xA9E1: { uint32_t h = A(0), s = slot_of(h); uint32_t nh = new_handle(hsize[s]); memcpy(ram + rd32(nh), ram + rd32(h), hsize[s]); SETA(0, nh); SETD(0, 0); break; }
    case 0xA9E3: { uint32_t nh = new_handle(D(0)); memcpy(ram + rd32(nh), ram + (A(0) & 0xFFFFFF), D(0)); SETA(0, nh); SETD(0, 0); break; }
    case 0xA9E2: case 0xA9EF: { uint32_t h = A(1), s = slot_of(h), old = hsize[s], cnt = D(0); set_handle_size(h, old + cnt); memcpy(ram + rd32(h) + old, ram + (A(0) & 0xFFFFFF), cnt); SETD(0, 0); break; }
    case 0xA9E4: { uint32_t src = A(0), dst = A(1), ss = hsize[slot_of(src)], ds = hsize[slot_of(dst)]; set_handle_size(dst, ds + ss); memcpy(ram + rd32(dst) + ds, ram + rd32(src), ss); SETD(0, 0); break; }
    /* ---- Trap table ---- */
    case 0xA046: { uint32_t k = trap_key(D(0), (w & 0x200) != 0); SETA(0, patched[k] ? patched[k] : STUB_BASE + k * 4); break; }
    case 0xA047: { uint32_t k = trap_key(D(0), (w & 0x200) != 0); patched[k] = A(0); if (trace_limit > 0) fprintf(stderr, "  SetTrapAddress key=%03x -> %06x\n", k, A(0)); break; }
    /* ---- Resource Manager ---- */
    case 0xA9A0: { uint32_t id = rd16(sp), type = rd32(sp + 2); Res *r = find_res(type, (int16_t)id); setsp(sp + 6); wr32(sp + 6, r ? res_get_handle(r) : 0); res_err = r ? 0 : -192; break; }
    case 0xA9A1: case 0xA820: {      /* GetNamedResource / Get1NamedResource(type, name) : Handle */
        char nm[256]; uint32_t np = rd32(sp), type = rd32(sp + 4); int l = rd8(np); for (int i = 0; i < l; i++) nm[i] = rd8(np + 1 + i); nm[l] = 0;
        uint32_t hh = 0; for (int i = 0; i < nres; i++) if (res[i].type == type && !strcasecmp(res[i].name, nm)) { hh = res_get_handle(&res[i]); break; }
        setsp(sp + 8); wr32(sp + 8, hh); res_err = hh ? 0 : -192; break; }
    case 0xA81F: { uint32_t id = rd16(sp), type = rd32(sp + 2); Res *r = find_res(type, (int16_t)id); setsp(sp + 6); wr32(sp + 6, r ? res_get_handle(r) : 0); res_err = r ? 0 : -192; break; }  /* Get1Resource */
    case 0xA80D: { uint32_t type = rd32(sp); int c = 0; for (int i = 0; i < nres; i++) if (res[i].type == type) c++; setsp(sp + 4); wr16(sp + 4, c); break; }                           /* Count1Resources */
    case 0xA80E: { uint32_t type = rd32(sp), idx = rd16(sp + 4); int c = 0; uint32_t hh = 0; for (int i = 0; i < nres; i++) if (res[i].type == type && ++c == (int)idx) { hh = res_get_handle(&res[i]); break; } setsp(sp + 6); wr32(sp + 6, hh); break; } /* Get1IndResource */
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
    case 0xA000: case 0xA001: case 0xA002: case 0xA003: case 0xA008: case 0xA009: case 0xA00A: case 0xA00C: case 0xA00D: case 0xA011: case 0xA012:
    case 0xA014: case 0xA015: case 0xA018: case 0xA044: case 0xA060: {
        if (trace_limit > 0) { fprintf(stderr, "    PB:"); for (int i = 0; i < 64; i += 2) fprintf(stderr, " %04x", rd16(A(0) + i)); fprintf(stderr, "\n"); }
        int err = fs_dispatch(w, n, D(0), A(0)); wr16(A(0) + 16, (uint16_t)err); SETD(0, (uint32_t)err);
        { uint32_t np = rd32(A(0) + 18); char nm[80] = ""; if (np && np < RAM_SIZE) { int l = rd8(np); if (l > 70) l = 70; for (int i = 0; i < l; i++) nm[i] = rd8(np + 1 + i); nm[l] = 0; }
          if (n != 0xA002 && n != 0xA003) fprintf(stderr, "  [fs] %04X \"%s\" vol/dir=%d/%u -> %d\n", w, nm, (int16_t)rd16(A(0) + 22), rd32(A(0) + 48), err); }
        if (trace_limit > 0) fprintf(stderr, "    -> FS err=%d\n", err); break; }
    case 0xA98B: {      /* ParamText(p0,p1,p2,p3) */
        for (int i = 0; i < 4; i++) { uint32_t p = rd32(sp + 12 - i * 4); char b[256]; int l = p ? rd8(p) : 0; for (int k = 0; k < l; k++) b[k] = rd8(p + 1 + k); b[l] = 0; fprintf(stderr, "  ParamText[%d]=\"%s\"\n", i, b); }
        setsp(sp + 16); break; }
    case 0xA9B1: { uint32_t p = rd32(sp); char b[256]; int l = rd8(p); for (int k = 0; k < l; k++) b[k] = rd8(p + 1 + k); b[l] = 0; fprintf(stderr, "  CreateResFile(\"%s\")\n", b); setsp(sp + 4); res_err = 0; break; }
    /* ---- Init / environment ---- */
    case 0xA97B: case 0xA851: setsp(sp + 4); break;                        /* InitDialogs, SetCursor */
    case 0xA8FE: case 0xA912: case 0xA930: case 0xA9CC: case 0xA850: case 0xA852: case 0xA853: case 0xA856: case 0xA9E6: break;
    case 0xA9C8: case 0xA9E5: setsp(sp + 2); break;                        /* SysBeep, InitPack */
    case 0xA032: SETD(0, 0); break;                                         /* FlushEvents */
    case 0xA03F: break;
    case 0xA975: setsp(sp); wr32(sp, ticks_now()); break;                  /* TickCount */
    case 0xA861: { uint32_t sd = rd32(cur_a5() - 4 - 126); sd = sd * 16807u % 2147483647u; wr32(cur_a5() - 4 - 126, sd); if (getenv("RNDLOG")) fprintf(stderr, "[rnd] %08x sp=%06x D=%x,%x,%x,%x,%x,%x A=%x,%x,%x,%x,%x\n", sd, sp, R(M68K_REG_D0), R(M68K_REG_D3), R(M68K_REG_D4), R(M68K_REG_D5), R(M68K_REG_D6), R(M68K_REG_D7), R(M68K_REG_A0), R(M68K_REG_A2), R(M68K_REG_A3), R(M68K_REG_A4), R(M68K_REG_A6)); wr16(sp, (int16_t)(sd & 0xFFFF)); break; }
    case 0xA970: case 0xA971: { uint32_t ev = rd32(sp); for (int i = 0; i < 16; i++) wr8(ev + i, 0); wr32(ev + 10, ticks_now()); setsp(sp + 6); wr16(sp + 6, 0); break; }
    case 0xA860: { uint32_t ev = rd32(sp + 8); for (int i = 0; i < 16; i++) wr8(ev + i, 0); wr32(ev + 10, ticks_now()); setsp(sp + 14); wr16(sp + 14, 0); break; }
    case 0xA974: case 0xA973: wr16(sp, 0); break;                           /* Button, StillDown */
    case 0xA972: { uint32_t pt = rd32(sp); wr32(pt, 0); setsp(sp + 4); break; }
    case 0xA090: { uint32_t p = A(0); wr16(p, 2); wr16(p + 2, 6); wr16(p + 4, 0x0710); wr16(p + 6, 3); wr8(p + 8, 1); wr8(p + 9, 1); wr16(p + 10, 3); wr16(p + 12, 0); wr16(p + 14, 0); SETD(0, 0); break; }
    case 0xA0AD: {
        uint32_t sel = D(0), v = 0; int ok = 1;
        switch (sel) { case 0x73797376: v = 0x0710; break; case 0x71642020: v = 0x0300; break; case 0x70726F63: v = 4; break;
                       case 0x66707520: v = 3; break; case 0x6D616368: v = 25; break; case 0x6B626420: v = 3; break; default: ok = 0; }
        SETA(0, v); SETD(0, ok ? 0 : (uint32_t)-5551); break; }
    /* ---- Segment loader / process ---- */
    case 0xA9F0: do_loadseg(ret); redirected = 1; break;
    case 0xA9F4: rm_flush_all(); die("ExitToShell (clean exit)"); redirected = 1; break;
    case 0xA9C9: die("SysError(%d)", (int16_t)D(0)); redirected = 1; break;
    default:
        if (gfx_dispatch(w, n, ret)) break;
        if (pal_dispatch(w, n, ret)) break;
        if (sane_dispatch(w, n, ret)) break;
        if (qd_dispatch(w, n, ret)) break;
        if (snd_dispatch(w, n, ret)) break;
        if (tasks_dispatch(w, n, ret)) break;
        if (qt_dispatch(w, n, ret)) break;
        if (misc_dispatch(w, n, ret)) break;
        if (n == 0xA9EA) fprintf(stderr, "P3 stack: %04x %04x%04x %04x%04x %04x%04x %04x%04x %04x%04x\n", rd16(SP), rd16(SP+2), rd16(SP+4), rd16(SP+6), rd16(SP+8), rd16(SP+10), rd16(SP+12), rd16(SP+14), rd16(SP+16), rd16(SP+18), rd16(SP+20));
        die("unimplemented trap %04X %s at ret=%06x  (D0=%08x A0=%08x A1=%08x SP=%06x)", w, tname(w), ret, D(0), A(0), A(1), SP);
        redirected = 1; break;
    }
    if (!redirected) W(M68K_REG_PC, ret);
}

unsigned long interp_insns; static unsigned *pchist;
static void det_report(void) {
    if (det_stop && ntraps >= det_stop) { quit_requested = 1; }
    if (det_every && det_slices % det_every == 0) { uint64_t hsh = 1469598103934665603ull; for (uint32_t i = 0; i < 0xC00000; i += 8) hsh = (hsh ^ *(uint64_t *)(ram + i)) * 1099511628211ull; fprintf(stderr, "[det] slice=%lu traps=%lu ram=%016llx\n", det_slices, ntraps, (unsigned long long)hsh); }
}
void castles_instr_hook(unsigned int pc) {
    for (;;) {
        
        if (pc == SENTINEL) { call_done = 1; m68k_end_timeslice(); return; }
        if (pc == YIELD_PC + 2) { pc = yield_resume; W(M68K_REG_PC, pc); }
        else if (pc == GCONT_PC) { gc_resume(); pc = R(M68K_REG_PC); }
        if (lift_chk_active && pc == chk_ret && SP == chk_sp) lift_check_finish();
#ifdef LIFT_TRACE
        if (lift_chk_active) tri_record(pc);
#endif
        if (det && ntraps >= det_next && (!det_ymax || det_slices < det_ymax) && !call_depth && !lift_dry && !halted && (rd16(pc) & 0xF000) == 0xA000) { det_next = ntraps + 128; det_slices++; det_yield = 1; det_report(); if (getenv("DET_VERB") && det_slices >= 47) fprintf(stderr, "[yield %lu] pc=%06x op=%04x sp=%06x stub=%d wmred=%d\n", det_slices, pc, rd16(pc), SP, pc >= STUB_BASE && pc < STUB_END, wm_redirected); yield_resume = pc; W(M68K_REG_PC, YIELD_PC); m68k_end_timeslice(); return; }
        { static int dw = -1; static unsigned da; static uint32_t dv; if (dw < 0) { const char *e = getenv("DWATCH"); dw = e ? 1 : 0; if (e) { da = strtoul(e, 0, 16); dv = rd32(da); } } if (dw == 1) { uint32_t nv = rd32(da); if (nv != dv) { fprintf(stderr, "[dwatch] %06x changed %08x -> %08x at pc=%06x D0=%x A0=%x A1=%x A2=%x A3=%x\n", da, dv, nv, pc, R(M68K_REG_D0), R(M68K_REG_A0), R(M68K_REG_A1), R(M68K_REG_A2), R(M68K_REG_A3)); dv = nv; } } }
        { static uint32_t ring[64]; static int rp, rdone; ring[rp++ & 63] = pc; static unsigned rl, rh; static int ri; if (!ri) { ri = 1; const char *e = getenv("RINGW"); if (e) sscanf(e, "%x:%x", &rl, &rh); } if (rh && !rdone && pc >= rl && pc < rh) { rdone = 1; fprintf(stderr, "[ring]"); for (int q = 40; q >= 0; q--) { uint32_t rpc = ring[(rp - 1 - q) & 63]; fprintf(stderr, " %06x%s", rpc, lift_lookup(rpc) ? "E" : lift_midlookup(rpc) ? "M" : ""); } fprintf(stderr, "\n"); } }
        { static int pw; static unsigned wl, wh; if (!pw) { pw = 1; const char *e = getenv("PCWATCH"); if (e) sscanf(e, "%x:%x", &wl, &wh); } if (wh && pc >= wl && pc < wh) fprintf(stderr, "[pcw] stk=%x,%x,%x,%x %06x D=%x,%x,%x,%x,%x,%x,%x,%x A=%x,%x,%x,%x,%x,%x,%x,%x sr=%x\n", rd32(SP), rd32(SP + 4), rd32(SP + 8), rd32(SP + 12), pc, R(M68K_REG_D0), R(M68K_REG_D1), R(M68K_REG_D2), R(M68K_REG_D3), R(M68K_REG_D4), R(M68K_REG_D5), R(M68K_REG_D6), R(M68K_REG_D7), R(M68K_REG_A0), R(M68K_REG_A1), R(M68K_REG_A2), R(M68K_REG_A3), R(M68K_REG_A4), R(M68K_REG_A5), R(M68K_REG_A6), SP, R(M68K_REG_SR) & 0x1f); }
        if (((pc >= lift_lo && pc < lift_hi) || lift_special(pc)) && lift_try(pc)) { pc = R(M68K_REG_PC); continue; }
        { static int ni = -1; if (ni < 0) ni = getenv("NOINTERP") != NULL; if (ni && (rd16(pc) & 0xF000) != 0xA000 && pc != YIELD_PC && pc != YIELD_PC + 2 && pc != GCONT_PC && pc != SENTINEL) { fprintf(stderr, "[nointerp] unlifted pc=%06x op=%04x sp=%06x\n", pc, rd16(pc), SP); if (getenv("NOINTERP")[0] == '2') quit_requested = 1; else { static int nn; if (nn++ > 40) quit_requested = 1; } } }
        interp_insns++; if (getenv("PCHIST")) { if (!pchist) pchist = calloc(RAM_SIZE / 2, 4); if (pc < RAM_SIZE) pchist[pc >> 1]++; }
        uint32_t op = rd16(pc); if (pc > 0x1000) pcring[pcri++ & 63] = pc;
        if ((op & 0xF000) != 0xA000 || halted) return;
        uint32_t ret;
        if (pc >= STUB_BASE && pc < STUB_END) ret = pop32();
        else {
            uint32_t k = trap_key(op, 0);
            if (patched[k]) { push32(pc + 2); W(M68K_REG_PC, patched[k]); pc = patched[k]; continue; }
            ret = pc + 2;
        }
        trap_pc = pc; trap_from_stub = (pc >= STUB_BASE && pc < STUB_END);
        hle((uint16_t)op, ret);
        if (!(op & 0x800) && !halted) { uint32_t d0 = R(M68K_REG_D0) & 0xFFFF; uint32_t sr = R(M68K_REG_SR) & ~0xFu; if (d0 == 0) sr |= 4; if (d0 & 0x8000) sr |= 8; W(M68K_REG_SR, sr); }  /* OS traps set CCR from D0.w */
        pc = R(M68K_REG_PC);
    }
}

#ifdef CASTLES_NATIVE_BUILD
uint32_t native_regs[19]; int native_end_slice;
int m68k_execute(int cycles) {
    native_end_slice = 0;
    for (int it = 0; it < cycles && !native_end_slice && !halted; it++) {
        uint32_t pc = native_regs[M68K_REG_PC];
        if (pc == YIELD_PC) { native_regs[M68K_REG_PC] = pc += 2; }       /* the NOP pair: step over the first, the hook resumes at +2 */
        castles_instr_hook(pc);
        if (native_end_slice || halted) break;
        if (native_regs[M68K_REG_PC] == pc && pc != YIELD_PC + 2) {
            fprintf(stderr, "[native] cannot execute pc=%06x op=%04x sp=%06x (not lifted; rebuild with the interpreter: ./build.sh)\n  recent lift entries:", pc, rd16(pc), SP); for (int q = 31; q >= 0; q--) fprintf(stderr, " %06x", lift_entring[(lift_entri - 1 - q) & 31]); fprintf(stderr, "\n  stack:"); for (int q = 0; q < 8; q++) fprintf(stderr, " %08x", rd32(SP + 4 * q)); fprintf(stderr, "\n");
            if (call_depth > 0) { fprintf(stderr, "  [native] abandoning host-called routine (stale/garbage target)\n"); call_done = 1; native_end_slice = 1; break; }   /* VBL/Time Manager/sound callback went astray: drop the call, keep playing */
            halted = 1; quit_requested = 1; break;
        }
    }
    return 0;
}
#endif
/* ---------------- Launch ---------------- */
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <game folder | app.bin | rsrc.fork> [game folder]\n", argv[0]); return 2; }
    det = getenv("CASTLES_DET") != NULL; if (getenv("DET_STOP")) det_stop = strtoul(getenv("DET_STOP"), 0, 10); if (getenv("DET_YMAX")) det_ymax = strtoul(getenv("DET_YMAX"), 0, 10); if (getenv("DET_EVERY")) det_every = strtoul(getenv("DET_EVERY"), 0, 10);
    if (getenv("LIFTCHECK")) { lift_check = 1; if (atoi(getenv("LIFTCHECK")) > 1) lift_chk_max = atoi(getenv("LIFTCHECK")); }
    if (getenv("NOLIFT")) lift_on = 0;
    load_fork(argv[1]); if (argc > 2) strncpy(game_root, argv[2], sizeof game_root - 1);
    for (uint32_t k = 0; k < 0x1000; k++) { wr16(STUB_BASE + k * 4, 0xA000 | (k >= 0x400 ? 0x800 | (k - 0x400) : k)); wr16(STUB_BASE + k * 4 + 2, 0x4E75); }

    wr16(SENTINEL, 0x4E71); wr16(SENTINEL + 2, 0x4E71); wr16(GCONT_PC, 0x4E71); wr16(GCONT_PC + 2, 0x4E71); wr16(YIELD_PC, 0x4E71); wr16(YIELD_PC + 2, 0x4E71);
    Res *c0 = find_res(0x434F4445, 0); if (!c0) { fprintf(stderr, "no CODE 0\n"); return 1; }
    const uint8_t *p = rfork + c0->off;
    uint32_t above = be32(p), below = be32(p + 4), jtlen = be32(p + 8), jtoff = be32(p + 12);
    uint32_t a5 = STACK_TOP + below;
    memcpy(ram + a5 + jtoff, p + 16, jtlen);
    fprintf(stderr, "A5=%06x below=%u above=%u jt=%u@+%u\n", a5, below, above, jtlen, jtoff);

    wr32(0x904, a5); wr32(0x908, STACK_TOP); wr32(0x130, HEAP_END);
    wr8(0x260, 0x07); wr32(0x2AA, HEAP_START); wr32(0x2A6, HEAP_START); wr32(0x108, RAM_SIZE); wr16(0x28E, 0);
    wr16(0x900, 1); /* CurApRefNum */
    { const char *an = "Castles - Siege & Conquest"; wr8(0x910, strlen(an)); for (size_t i = 0; i < strlen(an); i++) wr8(0x911 + i, an[i]); } /* CurApName */
    wr32(0, STACK_TOP - 8); wr32(4, a5 + jtoff + 2);

    m68k_set_cpu_type(M68K_CPU_TYPE_68030);
    m68k_init();
    m68k_pulse_reset();
    W(M68K_REG_A5, a5);
    setsp(STACK_TOP);
    push32(STUB_BASE + trap_key(0xA9F4, 0) * 4);     /* return-to-ExitToShell */
    W(M68K_REG_PC, a5 + jtoff + 2);

    plat_init(); wr16(0xBAA, 20);
    unsigned long guard = 0, maxslices = getenv("MAXSLICES") ? strtoul(getenv("MAXSLICES"), NULL, 10) : 0; uint32_t last_present = 0;
    while (!halted) {
        m68k_execute(10000); if (lift_chk_active && ++chk_slices > 300) { lift_chk_active = 0; lift_on = 1; chk_skip++; chk_slices = 0; } if (!lift_chk_active && (!det || det_yield)) { det_yield = 0; chk_slices = 0; wr32(0x16A, ticks_now()); wr32(0x20C, (uint32_t)((det ? 3900000000u - 2082844800u : time(NULL)) + 2082844800u)); run_tasks(); ev_pump(); }
        if (quit_requested) { rm_flush_all(); fprintf(stderr, "quit requested\n"); break; }
        uint32_t t = ticks_now(); if (t - last_present >= 2) { plat_present(); gfx_dirty = 0; last_present = t; }
        if (idle_hint) { idle_hint = 0; usleep(1500); }
        if (maxslices && ++guard >= maxslices) break;
    }
    if (getenv("CASTLES_SHOT")) { write_ppm(getenv("CASTLES_SHOT")); fprintf(stderr, "screenshot -> %s\n", getenv("CASTLES_SHOT")); }
    { fprintf(stderr, "trap histogram:"); for (int k = 0; k < 60; k++) { int b = -1; for (int i = 0; i < 65536; i++) if (thist[i] && (b < 0 || thist[i] > thist[b])) b = i; if (b < 0) break; fprintf(stderr, " %04X(%s)=%lu", b, tname(b), thist[b]); thist[b] = 0; } fprintf(stderr, "\n"); }
    { fprintf(stderr, "recent traps:\n"); for (int i = 0; i < 600; i++) { int k_ = (tri + i) % 600; if (tring[k_].w) fprintf(stderr, "  [trap %04X %-18s] D0=%08x A0=%08x A1=%08x SP=%06x ret=%06x\n", tring[k_].w, tname(tring[k_].w), tring[k_].d0, tring[k_].a0, tring[k_].a1, tring[k_].sp, tring[k_].ret); } }
    if (!halted) { fprintf(stderr, "last pcs:"); for (int i = 0; i < 40; i++) fprintf(stderr, " %06x", pcring[(pcri - 40 + i) & 63]); fprintf(stderr, "\n"); }
    { const char *dr = getenv("DUMPRAM"); if (dr) { FILE *f = fopen(dr, "wb"); if (f) { fwrite(ram, 1, 0xC10000, f); fclose(f); } } }
    { const char *cl = getenv("COVLOG"); if (cl) { FILE *f = fopen(cl, "a"); if (f) { for (int z = 0; z < 16; z++) for (int o = 0; o < (1 << 15); o++) if (lift_fcov[z][o]) fprintf(f, "%d:%x\n", z, o << 1); fclose(f); } } }
    { const char *el = getenv("ENTLOG"); if (el) { FILE *f = fopen(el, "a"); if (f) { for (uint32_t i = 0; i < (1u << 23); i++) if (lift_ebt[i]) { uint32_t a = i << 1; for (int z = 1; z < 16; z++) if (lift_segtab[z] && a >= SB[z] && a < SB[z] + lift_seglen[z]) { fprintf(f, "%d:%x\n", z, a - SB[z]); break; } } fclose(f); } } }
    if (lift_chk_active) fprintf(stderr, "[chkpc] check STILL ACTIVE at exit: entry=%06x ret=%06x sp=%x\n", chk_entry, chk_ret, chk_sp);
    if (lift_check) fprintf(stderr, "[LIFTCHECK] verified=%d mismatched=%d skipped(trap/bail)=%d (verified incl. partial-to-trap: %lu)\n", chk_ok, chk_bad, chk_skip, chk_part_n);
    if (pchist) { for (int k = 0; k < 400; k++) { unsigned best = 0; uint32_t bi = 0; for (uint32_t i = 0; i < RAM_SIZE / 2; i++) if (pchist[i] > best) { best = pchist[i]; bi = i; } if (!best) break; fprintf(stderr, "  hot %06x x%u  bytes:", bi * 2, best); for (int q = 0; q < 12; q++) fprintf(stderr, "%02x", ram[bi * 2 + q]); fprintf(stderr, "\n"); pchist[bi] = 0; } }
    if (hl_on > 0) { for (int pass = 0; pass < 12; pass++) { int bi = -1; unsigned long bs = 0; static uint32_t seen[12][2]; for (int k = 0; k < (1 << 16); k++) if (hl_live[k].p && hl_live[k].p != 0xFFFFFFFF) { unsigned long sz = 0; int cnt = 0; for (int j = 0; j < (1 << 16); j++) if (hl_live[j].p && hl_live[j].p != 0xFFFFFFFF && hl_live[j].n == hl_live[k].n && hl_live[j].pc == hl_live[k].pc) { sz += hl_live[j].n; cnt++; } int dup = 0; for (int q = 0; q < pass; q++) if (seen[q][0] == hl_live[k].n && seen[q][1] == hl_live[k].pc) dup = 1; if (!dup && sz > bs) { bs = sz; bi = k; } } if (bi < 0) break; seen[pass][0] = hl_live[bi].n; seen[pass][1] = hl_live[bi].pc; int cnt = 0; for (int j = 0; j < (1 << 16); j++) if (hl_live[j].p && hl_live[j].p != 0xFFFFFFFF && hl_live[j].n == hl_live[bi].n && hl_live[j].pc == hl_live[bi].pc) cnt++; fprintf(stderr, "[heap] live: %d x %u bytes (total %lu) allocated at trap pc=%06x\n", cnt, hl_live[bi].n, bs, hl_live[bi].pc); } }
    if (getenv("HEAPLOG")) fprintf(stderr, "[heap] at exit: free=%u maxblock=%u extents=%d slots=%u freeslots=%u\n", heap_free, heap_maxblock(), nfl, next_slot, nfree_slots);
    if (lift_calls) { fprintf(stderr, "bails: enter-unlifted=%lu; redirecting traps:", lift_enterbail); for (int k = 0; k < 12; k++) { int b = -1; for (int i = 0; i < 65536; i++) if (lift_trapbail[i] && (b < 0 || lift_trapbail[i] > lift_trapbail[b])) b = i; if (b < 0) break; fprintf(stderr, " %04X(%s)=%lu", b, tname(b), lift_trapbail[b]); lift_trapbail[b] = 0; } fprintf(stderr, "\n"); }
    if (lift_calls) { fprintf(stderr, "top bail targets:"); for (int k = 0; k < 12; k++) { unsigned best = 0; uint32_t bi = 0; for (uint32_t i = 0; i < (1 << 23); i++) if (lift_bh[i] > best) { best = lift_bh[i]; bi = i; } if (!best) break; fprintf(stderr, " %06x=%u", bi << 1, best); lift_bh[bi] = 0; } fprintf(stderr, "\n"); }
    if (lift_calls) { fprintf(stderr, "blocked=%lu guestcalls=%lu\n", lift_blocked, lift_guestcalls); fprintf(stderr, "unlifted enter targets:"); for (int k = 0; k < 10; k++) { unsigned best = 0; uint32_t bi = 0; for (uint32_t i = 0; i < (1 << 23); i++) if (lift_ebt[i] > best) { best = lift_ebt[i]; bi = i; } if (!best) break; fprintf(stderr, " %06x=%u", bi * 2, best); lift_ebt[bi] = 0; } fprintf(stderr, "\n"); }
    fprintf(stderr, "interp hook insns=%lu\n", interp_insns), fprintf(stderr, "lift: calls=%lu bails=%lu traps=%lu\n", lift_calls, lift_bails, lift_traps);
    fprintf(stderr, "stopped: traps=%lu pc=%06x sp=%06x\n", ntraps, R(M68K_REG_PC), SP);
    if (getenv("CASTLES_DUMP")) { FILE *df = fopen("build/ram.bin", "wb"); if (df) { fwrite(ram, 1, RAM_SIZE, df); fclose(df); } }
    return halted ? 0 : 3;
}
