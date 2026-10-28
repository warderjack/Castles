/* ---- QuickDraw / Color QuickDraw / GWorld HLE (stage A: devices, ports, pixmaps, regions, rect utils) ---- */
typedef struct { int16_t t, l, b, r; } Rc;
static Rc rc_get(uint32_t a) { Rc r = {(int16_t)rd16(a), (int16_t)rd16(a + 2), (int16_t)rd16(a + 4), (int16_t)rd16(a + 6)}; return r; }
static void rc_put(uint32_t a, Rc r) { wr16(a, r.t); wr16(a + 2, r.l); wr16(a + 4, r.b); wr16(a + 6, r.r); }
static int rc_empty(Rc r) { return r.b <= r.t || r.r <= r.l; }
static Rc rc_sect(Rc a, Rc b) { Rc r = {a.t > b.t ? a.t : b.t, a.l > b.l ? a.l : b.l, a.b < b.b ? a.b : b.b, a.r < b.r ? a.r : b.r}; return r; }

static uint32_t qdP = 0, main_gd = 0, main_pm = 0, main_ct = 0, screen_port = 0, cur_gd = 0, ctseed = 1000;
#define SCR_W 640
#define SCR_H 480

static void std_clut(uint32_t ct) {
    wr32(ct, 0); wr16(ct + 4, 0); wr16(ct + 6, 255);
    for (int i = 0; i < 256; i++) {
        uint32_t r, g, b;
        if (i < 215) { r = 0xFFFF - (i / 36) * 0x3333; g = 0xFFFF - ((i / 6) % 6) * 0x3333; b = 0xFFFF - (i % 6) * 0x3333; }
        else if (i < 255) {
            static const uint16_t ramp[10] = {0xEEEE,0xDDDD,0xBBBB,0xAAAA,0x8888,0x7777,0x5555,0x4444,0x2222,0x1111};
            int k = i - 215, grp = k / 10, v = ramp[k % 10]; r = grp == 0 ? v : 0; g = grp == 1 ? v : 0; b = grp == 2 ? v : 0;
            if (grp == 3) r = g = b = v;
        } else r = g = b = 0;
        uint32_t e = ct + 8 + i * 8; wr16(e, i); wr16(e + 2, r); wr16(e + 4, g); wr16(e + 6, b);
    }
}
static uint32_t new_ctab(int n) { uint32_t h = new_handle(8 + 8 * n); wr32(rd32(h), ++ctseed); wr16(rd32(h) + 6, n - 1); return h; }

/* ---- regions: rect-list representation inside Mac-style handle (size 10 = plain rect) ---- */
typedef struct { Rc *r; int n, cap; } RL;
static void rl_add(RL *l, Rc r) { if (rc_empty(r)) return; if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 8; l->r = realloc(l->r, l->cap * sizeof(Rc)); } l->r[l->n++] = r; }
static void rl_free(RL *l) { free(l->r); l->r = NULL; l->n = l->cap = 0; }
static void rl_minus_rect(const RL *in, Rc c, RL *out) {
    for (int i = 0; i < in->n; i++) {
        Rc a = in->r[i], s = rc_sect(a, c);
        if (rc_empty(s)) { rl_add(out, a); continue; }
        Rc top = {a.t, a.l, s.t, a.r}, bot = {s.b, a.l, a.b, a.r}, lf = {s.t, a.l, s.b, s.l}, rt = {s.t, s.r, s.b, a.r};
        rl_add(out, top); rl_add(out, bot); rl_add(out, lf); rl_add(out, rt);
    }
}
static void rl_diff(const RL *a, const RL *b, RL *out) {
    RL cur = {0}; for (int i = 0; i < a->n; i++) rl_add(&cur, a->r[i]);
    for (int j = 0; j < b->n; j++) { RL nx = {0}; rl_minus_rect(&cur, b->r[j], &nx); rl_free(&cur); cur = nx; }
    for (int i = 0; i < cur.n; i++) rl_add(out, cur.r[i]); rl_free(&cur);
}
static void rl_sect(const RL *a, const RL *b, RL *out) { for (int i = 0; i < a->n; i++) for (int j = 0; j < b->n; j++) rl_add(out, rc_sect(a->r[i], b->r[j])); }
static void rl_union(const RL *a, const RL *b, RL *out) { for (int i = 0; i < a->n; i++) rl_add(out, a->r[i]); RL d = {0}; rl_diff(b, a, &d); for (int i = 0; i < d.n; i++) rl_add(out, d.r[i]); rl_free(&d); }
static void rgn_get(uint32_t h, RL *out) {
    if (!h) return; uint32_t p = rd32(h); if (!p) return; uint32_t sz = rd16(p);
    if (sz <= 10) rl_add(out, rc_get(p + 2)); else for (uint32_t o = 10; o + 8 <= sz; o += 8) rl_add(out, rc_get(p + o));
}
static void rgn_set(uint32_t h, const RL *l) {
    Rc bb = {0, 0, 0, 0}; int first = 1;
    for (int i = 0; i < l->n; i++) { Rc r = l->r[i]; if (first) { bb = r; first = 0; } else { if (r.t < bb.t) bb.t = r.t; if (r.l < bb.l) bb.l = r.l; if (r.b > bb.b) bb.b = r.b; if (r.r > bb.r) bb.r = r.r; } }
    uint32_t sz = l->n <= 1 ? 10 : 10 + 8 * l->n;
    set_handle_size(h, sz); uint32_t p = rd32(h); wr16(p, sz); rc_put(p + 2, bb);
    if (l->n > 1) for (int i = 0; i < l->n; i++) rc_put(p + 10 + 8 * i, l->r[i]);
}
static uint32_t new_rgn(void) { uint32_t h = new_handle(10); wr16(rd32(h), 10); return h; }
static void rgn_set_rect(uint32_t h, Rc r) { RL l = {0}; if (!rc_empty(r)) rl_add(&l, r); rgn_set(h, &l); rl_free(&l); }
static int rgn_contains(uint32_t h, int x, int y) { RL l = {0}; rgn_get(h, &l); int in = 0; for (int i = 0; i < l.n; i++) if (y >= l.r[i].t && y < l.r[i].b && x >= l.r[i].l && x < l.r[i].r) in = 1; rl_free(&l); return in; }
static int rgn_empty(uint32_t h) { uint32_t p = rd32(h); return rd16(p) <= 10 ? rc_empty(rc_get(p + 2)) : 0; }

/* ---- ports ---- */
static void init_port_fields(uint32_t p, int color, Rc bounds) {
    wr16(p + 0, 0);
    uint32_t vis = new_rgn(), clip = new_rgn(); rgn_set_rect(vis, bounds); rgn_set_rect(clip, (Rc){-32767, -32767, 32767, 32767});
    rc_put(p + 16, bounds); wr32(p + 24, vis); wr32(p + 28, clip);
    for (int i = 0; i < 8; i++) { wr8(p + 32 + i, 0x00); wr8(p + 40 + i, 0xFF); wr8(p + 58 + i, 0xFF); }   /* bkPat white, fillPat black, pnPat black */
    wr32(p + 48, 0); wr32(p + 52, 0x00010001); wr16(p + 56, 8); wr16(p + 66, 0);
    wr16(p + 68, 0); wr8(p + 70, 0); wr16(p + 72, 1); wr16(p + 74, 12); wr32(p + 76, 0);
    wr32(p + 80, 33); wr32(p + 84, 30); wr16(p + 88, 0); wr16(p + 90, 0); wr32(p + 92, 0); wr32(p + 96, 0); wr32(p + 100, 0); wr32(p + 104, 0);
    if (color) { wr16(p + 6, 0xC000 | 0); wr16(p + 14, 0); /* portVersion */
        wr16(p + 36, 0); wr16(p + 38, 0); wr16(p + 40, 0); /* placeholder overwritten below for rgb colors */ }
}
static uint32_t port_pixmap(uint32_t port, int *is_c) {            /* returns address of PixMap/BitMap struct */
    if (rd16(port + 6) & 0xC000) { if (is_c) *is_c = 1; return rd32(rd32(port + 2)); }
    if (is_c) *is_c = 0; return port + 2;
}
static void setup_cport(uint32_t p, uint32_t pmh, Rc bounds) {
    init_port_fields(p, 1, bounds);
    wr32(p + 2, pmh); wr16(p + 6, 0xC000);
    /* CGrafPort field positions differ from GrafPort for patterns/colors */
    for (int i = 0; i < 6; i++) { wr8(p + 36 + i, 0); wr8(p + 42 + i, 0xFF); }   /* rgbFgColor black, rgbBkColor white */
    wr32(p + 32, 0); wr32(p + 58, 0); wr32(p + 62, 0);                             /* pixel patterns none */
    for (int i = 0; i < 8; i++) wr8(p + 100 - 0 + 0, rd8(p + 100));                 /* no-op */
}
static void std_port_color_cache(uint32_t p);
static uint32_t make_main_device(void) {
    main_ct = new_ctab(256); std_clut(rd32(main_ct));
    main_pm = new_handle(50); uint32_t pm = rd32(main_pm);
    wr32(pm, FB_BASE); wr16(pm + 4, 0x8000 | SCR_W); rc_put(pm + 6, (Rc){0, 0, SCR_H, SCR_W});
    wr16(pm + 14, 0); wr16(pm + 16, 0); wr32(pm + 18, 0); wr32(pm + 22, 0x00480000); wr32(pm + 26, 0x00480000);
    wr16(pm + 30, 0); wr16(pm + 32, 8); wr16(pm + 34, 1); wr16(pm + 36, 8); wr32(pm + 38, 0); wr32(pm + 42, main_ct); wr32(pm + 46, 0);
    main_gd = new_handle(62); uint32_t gd = rd32(main_gd);
    wr16(gd + 0, 0); wr16(gd + 2, 0); wr16(gd + 4, 2); wr32(gd + 6, 0); wr16(gd + 10, 0); wr32(gd + 12, 0); wr32(gd + 16, 0);
    wr16(gd + 20, 0xC000 | 0x0001 | 0x2000 | 0x0800 /* gdDevType color, screen device, main device, active */ );
    wr32(gd + 22, main_pm); wr32(gd + 26, 0); wr32(gd + 30, 0); rc_put(gd + 34, (Rc){0, 0, SCR_H, SCR_W}); wr32(gd + 42, 0);
    cur_gd = main_gd;
    /* screen port (the desktop / "wMgr" port) */
    screen_port = alloc_block(108); setup_cport(screen_port, main_pm, (Rc){0, 0, SCR_H, SCR_W});
    return main_gd;
}
static uint32_t the_port(void) { return qdP ? rd32(qdP) : 0; }
static void set_the_port(uint32_t p) { if (qdP) wr32(qdP, p); }

static uint32_t gw_new(int depth, Rc bounds, uint32_t ctab, uint32_t *gdout) {
    int w = bounds.r - bounds.l, h = bounds.b - bounds.t; if (w < 0) w = 0; if (h < 0) h = 0;
    if (depth == 0) depth = 8;
    uint32_t rb = (((uint32_t)w * depth + 31) / 32) * 4; if (rb > 0x3FFE) rb = 0x3FFE;
    uint32_t base = alloc_block(rb * (h ? h : 1));
    uint32_t pmh = new_handle(50), pm = rd32(pmh);
    wr32(pm, base); wr16(pm + 4, 0x8000 | rb); rc_put(pm + 6, bounds); wr16(pm + 14, 0); wr16(pm + 16, 0); wr32(pm + 22, 0x00480000); wr32(pm + 26, 0x00480000);
    wr16(pm + 30, depth > 8 ? 16 : 0); wr16(pm + 32, depth); wr16(pm + 34, depth > 8 ? 3 : 1); wr16(pm + 36, depth > 8 ? (depth == 16 ? 5 : 8) : depth);
    if (depth <= 8) { if (!ctab) { if (depth == 8) ctab = main_ct; else ctab = new_ctab(1 << depth); } wr32(pm + 42, ctab); }
    uint32_t port = alloc_block(108); setup_cport(port, pmh, bounds);
    uint32_t gdh = new_handle(62), gd = rd32(gdh); memcpy(ram + gd, ram + rd32(main_gd), 62);
    wr32(gd + 22, pmh); rc_put(gd + 34, bounds); wr16(gd + 20, 0x4000 | (depth <= 8 ? 0x8000 : 0)); /* offscreen, not screen device */
    wr32(port + 0, 0); wr32(port + 8, gdh);                                                         /* grafVars */
    if (gdout) *gdout = gdh;
    return port;
}
static void wr_bool(uint32_t a, int v) { wr16(a, v ? 0x0100 : 0); }
static uint32_t gworld_devs_port[64], gworld_devs_gd[64]; static int ngw = 0;

/* Returns 1 if handled. sp = stack pointer at trap, may adjust stack. */
static int qd_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    uint32_t sp = SP;
    switch (n) {
    /* ---- GDevice ---- */
    case 0xAA29: setsp(sp); wr32(sp, main_gd); return 1;                    /* GetDeviceList */
    case 0xAA2A: setsp(sp); wr32(sp, main_gd); return 1;                    /* GetMainDevice */
    case 0xAA2B: setsp(sp + 4); wr32(sp + 4, 0); return 1;                  /* GetNextDevice */
    case 0xAA32: setsp(sp); wr32(sp, cur_gd); return 1;                     /* GetGDevice */
    case 0xAA31: cur_gd = rd32(sp); setsp(sp + 4); return 1;                /* SetGDevice */
    case 0xAA2C: setsp(sp + 6); wr_bool(sp + 6, 1); return 1;                  /* TestDeviceAttribute(gd, attr): bool -- main/screen/active => true */
    /* ---- ports ---- */
    case 0xA86E: init_graf(rd32(sp)); qdP = rd32(sp); setsp(sp + 4);
        make_main_device(); { uint32_t P = qdP; wr32(P - 122, FB_BASE); wr32(P, screen_port); } return 1;
    case 0xA873: set_the_port(rd32(sp)); setsp(sp + 4); return 1;           /* SetPort */
    case 0xA874: wr32(rd32(sp), the_port()); setsp(sp + 4); return 1;       /* GetPort */
    case 0xA86F: case 0xA86D: { uint32_t p = rd32(sp); init_port_fields(p, 0, (Rc){0, 0, SCR_H, SCR_W}); /* OpenPort/InitPort: screen bitmap */
        wr32(p + 2, FB_BASE); wr16(p + 6, SCR_W); rc_put(p + 8, (Rc){0, 0, SCR_H, SCR_W}); if (n == 0xA86F) set_the_port(p); setsp(sp + 4); return 1; }
    case 0xAA00: case 0xAA01: { uint32_t p = rd32(sp); setup_cport(p, main_pm, (Rc){0, 0, SCR_H, SCR_W}); if (n == 0xAA00) set_the_port(p); setsp(sp + 4); return 1; }
    case 0xA87D: case 0xAA02: setsp(sp + 4); return 1;                      /* ClosePort / CloseCPort */
    /* ---- GWorld (AB1D) ---- */
    case 0xAB1D: {
        uint32_t sel = D(0) & 0xFFFF;
        switch (sel) {
        case 0: { uint32_t flags = rd32(sp), gdev = rd32(sp + 4), ctab = rd32(sp + 8), bnd = rd32(sp + 12); int depth = (int16_t)rd16(sp + 16); uint32_t gwp = rd32(sp + 18);
                  (void)flags; (void)gdev; uint32_t gdh = 0; uint32_t port = gw_new(depth, rc_get(bnd), ctab, &gdh); wr32(gwp, port);
                  setsp(sp + 22); wr16(sp + 22, 0); return 1; }
        case 1: setsp(sp + 4); wr_bool(sp + 4, 1); return 1;                 /* LockPixels */
        case 2: case 7: case 8: case 9: case 10: case 11: case 12: case 4: setsp(sp + 4); return 1;
        case 3: setsp(sp + 22); wr32(sp + 22, 0); return 1;                  /* UpdateGWorld (no-op) */
        case 5: { uint32_t gdp = rd32(sp), pp = rd32(sp + 4); wr32(pp, the_port()); wr32(gdp, cur_gd); setsp(sp + 8); return 1; }
        case 6: { uint32_t gd = rd32(sp), port = rd32(sp + 4); set_the_port(port); if (gd) cur_gd = gd; setsp(sp + 8); return 1; }
        case 13: setsp(sp + 4); wr32(sp + 4, 0); return 1;
        case 14: setsp(sp + 8); return 1;
        case 15: { uint32_t pm = rd32(rd32(sp)); setsp(sp + 4); wr32(sp + 4, rd32(pm)); return 1; }
        case 18: { uint32_t port = rd32(sp); setsp(sp + 4); wr32(sp + 4, rd32(port + 8)); return 1; }
        case 19: setsp(sp + 4); wr_bool(sp + 4, 1); return 1;
        case 20: setsp(sp); wr32(sp, 0); return 1;
        case 22: setsp(sp + 4); wr_bool(sp + 4, 1); return 1;
        case 23: { uint32_t port = rd32(sp); setsp(sp + 4); wr32(sp + 4, rd32(port + 2)); return 1; }
        default: fprintf(stderr, "  GWorld selector %u (D0=%08x) not implemented\n", sel, D(0)); return 0;
        } }
    /* ---- pixmaps / color tables ---- */
    case 0xAA03: { uint32_t h = new_handle(50); wr16(rd32(h) + 4, 0x8000); setsp(sp); wr32(sp, h); return 1; }       /* NewPixMap */
    case 0xAA04: setsp(sp + 4); return 1;                                                                           /* DisposPixMap */
    case 0xAA18: { int id = (int16_t)rd16(sp); Res *r = find_res(0x636C7574 /*clut*/, id); uint32_t h = 0;
                   if (r) { h = new_handle(r->len); memcpy(ram + rd32(h), r->buf + r->off, r->len); wr32(rd32(h), ++ctseed); } else if (id == 8) { h = new_ctab(256); std_clut(rd32(h)); }
                   setsp(sp + 2); wr32(sp + 2, h); return 1; }                                                      /* GetCTable */
    /* ---- rect / point utilities ---- */
    case 0xA8A7: { Rc r = {(int16_t)rd16(sp + 4), (int16_t)rd16(sp + 6), (int16_t)rd16(sp), (int16_t)rd16(sp + 2)}; rc_put(rd32(sp + 8), r); setsp(sp + 12); return 1; }
    case 0xA8A8: { uint32_t a = rd32(sp + 4); int dv = (int16_t)rd16(sp), dh = (int16_t)rd16(sp + 2); Rc r = rc_get(a); r.t += dv; r.b += dv; r.l += dh; r.r += dh; rc_put(a, r); setsp(sp + 8); return 1; }
    case 0xA8A9: { uint32_t a = rd32(sp + 4); int dv = (int16_t)rd16(sp), dh = (int16_t)rd16(sp + 2); Rc r = rc_get(a); r.t += dv; r.b -= dv; r.l += dh; r.r -= dh; if (rc_empty(r)) r = (Rc){0, 0, 0, 0}; rc_put(a, r); setsp(sp + 8); return 1; }
    case 0xA8AA: { Rc a = rc_get(rd32(sp + 8)), b = rc_get(rd32(sp + 4)), s = rc_sect(a, b); int ok = !rc_empty(s); if (!ok) s = (Rc){0, 0, 0, 0}; rc_put(rd32(sp), s); setsp(sp + 12); wr_bool(sp + 12, ok); return 1; }
    case 0xA8AB: { Rc a = rc_get(rd32(sp + 8)), b = rc_get(rd32(sp + 4)), u;
                   if (rc_empty(a)) u = b; else if (rc_empty(b)) u = a; else { u.t = a.t < b.t ? a.t : b.t; u.l = a.l < b.l ? a.l : b.l; u.b = a.b > b.b ? a.b : b.b; u.r = a.r > b.r ? a.r : b.r; }
                   rc_put(rd32(sp), u); setsp(sp + 12); return 1; }
    case 0xA8AC: { int16_t v1 = rd16(sp + 8), h1 = rd16(sp + 10), v2 = rd16(sp + 4), h2 = rd16(sp + 6); Rc r = {v1 < v2 ? v1 : v2, h1 < h2 ? h1 : h2, v1 > v2 ? v1 : v2, h1 > h2 ? h1 : h2}; rc_put(rd32(sp), r); setsp(sp + 12); return 1; }
    case 0xA8AD: { Rc r = rc_get(rd32(sp)); int16_t v = rd16(sp + 4), h = rd16(sp + 6); int in = v >= r.t && v < r.b && h >= r.l && h < r.r; setsp(sp + 8); wr_bool(sp + 8, in); return 1; }
    case 0xA8AE: { int e = rc_empty(rc_get(rd32(sp))); setsp(sp + 4); wr_bool(sp + 4, e); return 1; }
    case 0xA8A6: { Rc a = rc_get(rd32(sp + 4)), b = rc_get(rd32(sp)); int e = !memcmp(&a, &b, sizeof a); setsp(sp + 8); wr_bool(sp + 8, e); return 1; }
    case 0xA87E: { uint32_t d = rd32(sp); wr16(d, rd16(d) + rd16(sp + 4)); wr16(d + 2, rd16(d + 2) + rd16(sp + 6)); setsp(sp + 8); return 1; }   /* AddPt */
    case 0xA87F: { uint32_t d = rd32(sp); wr16(d, rd16(d) - rd16(sp + 4)); wr16(d + 2, rd16(d + 2) - rd16(sp + 6)); setsp(sp + 8); return 1; }   /* SubPt */
    case 0xA880: { uint32_t d = rd32(sp + 4); wr16(d, rd16(sp)); wr16(d + 2, rd16(sp + 2)); setsp(sp + 8); return 1; }                          /* SetPt(pt,h,v) */
    case 0xA881: { int e = rd32(sp) == rd32(sp + 4); setsp(sp + 8); wr_bool(sp + 8, e); return 1; }
    case 0xA870: case 0xA871: {                                                                                                                 /* LocalToGlobal / GlobalToLocal */
        uint32_t pt = rd32(sp); uint32_t port = the_port(); int is_c; uint32_t pm = port_pixmap(port, &is_c); Rc b = rc_get(pm + (is_c ? 6 : 6));
        int16_t v = rd16(pt), h = rd16(pt + 2); if (n == 0xA870) { v -= b.t; h -= b.l; } else { v += b.t; h += b.l; } wr16(pt, v); wr16(pt + 2, h); setsp(sp + 4); return 1; }
    /* ---- regions ---- */
    case 0xA8D8: { uint32_t h = new_rgn(); setsp(sp); wr32(sp, h); return 1; }                                      /* NewRgn */
    case 0xA8D9: setsp(sp + 4); return 1;                                                                           /* DisposeRgn */
    case 0xA8DC: { RL l = {0}; rgn_get(rd32(sp + 4), &l); rgn_set(rd32(sp), &l); rl_free(&l); setsp(sp + 8); return 1; }   /* CopyRgn(src,dst) */
    case 0xA8DD: { RL l = {0}; rgn_set(rd32(sp), &l); setsp(sp + 4); return 1; }                                    /* SetEmptyRgn */
    case 0xA8DE: { Rc r = {(int16_t)rd16(sp + 4), (int16_t)rd16(sp + 6), (int16_t)rd16(sp), (int16_t)rd16(sp + 2)}; rgn_set_rect(rd32(sp + 8), r); setsp(sp + 12); return 1; } /* SetRectRgn(rgn,l,t,r,b) */
    case 0xA8DF: { rgn_set_rect(rd32(sp + 4), rc_get(rd32(sp))); setsp(sp + 8); return 1; }                        /* RectRgn(rgn, rect) */
    case 0xA8E0: { RL l = {0}; rgn_get(rd32(sp + 4), &l); int dv = (int16_t)rd16(sp), dh = (int16_t)rd16(sp + 2); for (int i = 0; i < l.n; i++) { l.r[i].t += dv; l.r[i].b += dv; l.r[i].l += dh; l.r[i].r += dh; } rgn_set(rd32(sp + 4), &l); rl_free(&l); setsp(sp + 8); return 1; }
    case 0xA8E1: { RL l = {0}, o = {0}; rgn_get(rd32(sp + 4), &l); int dv = (int16_t)rd16(sp), dh = (int16_t)rd16(sp + 2); for (int i = 0; i < l.n; i++) { Rc r = l.r[i]; r.t += dv; r.b -= dv; r.l += dh; r.r -= dh; rl_add(&o, r); } rgn_set(rd32(sp + 4), &o); rl_free(&l); rl_free(&o); setsp(sp + 8); return 1; }
    case 0xA8E2: { int e = rgn_empty(rd32(sp)); setsp(sp + 4); wr_bool(sp + 4, e); return 1; }                     /* EmptyRgn */
    case 0xA8E3: { RL a = {0}, b = {0}, d1 = {0}, d2 = {0}; rgn_get(rd32(sp + 4), &a); rgn_get(rd32(sp), &b); rl_diff(&a, &b, &d1); rl_diff(&b, &a, &d2);
                   int e = !d1.n && !d2.n; rl_free(&a); rl_free(&b); rl_free(&d1); rl_free(&d2); setsp(sp + 8); wr_bool(sp + 8, e); return 1; }
    case 0xA8E4: case 0xA8E5: case 0xA8E6: case 0xA8E7: {                                                           /* Sect / Union / Diff / Xor (a, b, dst) */
        RL a = {0}, b = {0}, o = {0}; rgn_get(rd32(sp + 8), &a); rgn_get(rd32(sp + 4), &b);
        if (n == 0xA8E4) rl_sect(&a, &b, &o); else if (n == 0xA8E5) rl_union(&a, &b, &o); else if (n == 0xA8E6) rl_diff(&a, &b, &o);
        else { RL d2 = {0}; rl_diff(&a, &b, &o); rl_diff(&b, &a, &d2); for (int i = 0; i < d2.n; i++) rl_add(&o, d2.r[i]); rl_free(&d2); }
        rgn_set(rd32(sp), &o); rl_free(&a); rl_free(&b); rl_free(&o); setsp(sp + 12); return 1; }
    case 0xA8E8: { int in = rgn_contains(rd32(sp), (int16_t)rd16(sp + 6), (int16_t)rd16(sp + 4)); setsp(sp + 8); wr_bool(sp + 8, in); return 1; }   /* PtInRgn(pt, rgn) */
    case 0xA8E9: { RL a = {0}, b = {0}, o = {0}; rgn_get(rd32(sp + 4), &a); rl_add(&b, rc_get(rd32(sp))); rl_sect(&a, &b, &o); int in = o.n > 0; rl_free(&a); rl_free(&b); rl_free(&o); setsp(sp + 8); wr_bool(sp + 8, in); return 1; }
    /* ---- clip ---- */
    case 0xA879: { uint32_t port = the_port(), rgn = rd32(sp); RL l = {0}; rgn_get(rgn, &l); rgn_set(rd32(port + 28), &l); rl_free(&l); setsp(sp + 4); return 1; }  /* SetClip */
    case 0xA87A: { uint32_t port = the_port(), rgn = rd32(sp); RL l = {0}; rgn_get(rd32(port + 28), &l); rgn_set(rgn, &l); rl_free(&l); setsp(sp + 4); return 1; }   /* GetClip */
    case 0xA87B: { uint32_t port = the_port(); rgn_set_rect(rd32(port + 28), rc_get(rd32(sp))); setsp(sp + 4); return 1; }                                            /* ClipRect */
    default: return 0;
    }
}
