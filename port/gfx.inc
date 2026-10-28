/* ---- QuickDraw stage B: rasterizer. Surfaces, patterns, shapes, lines, text, CopyBits, PICT ---- */
#include "fontdata.h"
#include <math.h>

typedef struct { uint16_t r, g, b; } RGB;
typedef struct { uint32_t base; int rb, depth; Rc bnd; uint32_t ctab; int color; } Surf;

static int gfx_dirty = 0;

static void surf_from_pm(uint32_t pm, Surf *s) {
    uint32_t rbw = rd16(pm + 4);
    if ((rbw & 0xC000) == 0xC000) { pm = rd32(rd32(pm)); rbw = rd16(pm + 4); }      /* &cgrafport->portPixMap passed as a BitMap* */
    s->base = rd32(pm); s->rb = rbw & 0x3FFF; s->bnd = rc_get(pm + 6);
    if (rbw & 0x8000) { s->depth = rd16(pm + 32); s->ctab = rd32(pm + 42); s->color = 1; } else { s->depth = 1; s->ctab = 0; s->color = 0; }
    if (s->depth == 0) s->depth = 1;
    if (!(rbw & 0x8000) && s->base == FB_BASE) { s->depth = 8; s->ctab = main_ct; s->color = 1; }       /* classic port on the 8-bit screen */
}
static int port_is_color(uint32_t port) { return (rd16(port + 6) & 0xC000) == 0xC000; }
static void surf_from_port(uint32_t port, Surf *s) {
    if (port_is_color(port)) surf_from_pm(rd32(rd32(port + 2)), s); else surf_from_pm(port + 2, s);
}

static inline uint32_t gp(const Surf *s, int x, int y) {
    x -= s->bnd.l; y -= s->bnd.t; const uint8_t *p = ram + ((s->base + (uint32_t)y * s->rb) & 0xFFFFFF);
    switch (s->depth) {
    case 8: return p[x];
    case 1: return (p[x >> 3] >> (7 - (x & 7))) & 1;
    case 2: return (p[x >> 2] >> (6 - 2 * (x & 3))) & 3;
    case 4: return (p[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
    case 16: return (p[2 * x] << 8) | p[2 * x + 1];
    default: return (p[4 * x] << 24) | (p[4 * x + 1] << 16) | (p[4 * x + 2] << 8) | p[4 * x + 3];
    }
}
static inline void pp(const Surf *s, int x, int y, uint32_t v) {
    x -= s->bnd.l; y -= s->bnd.t; uint8_t *p = ram + ((s->base + (uint32_t)y * s->rb) & 0xFFFFFF);
    switch (s->depth) {
    case 8: p[x] = v; break;
    case 1: { int sh = 7 - (x & 7); p[x >> 3] = (p[x >> 3] & ~(1 << sh)) | ((v & 1) << sh); break; }
    case 2: { int sh = 6 - 2 * (x & 3); p[x >> 2] = (p[x >> 2] & ~(3 << sh)) | ((v & 3) << sh); break; }
    case 4: { int sh = (x & 1) ? 0 : 4; p[x >> 1] = (p[x >> 1] & ~(15 << sh)) | ((v & 15) << sh); break; }
    case 16: p[2 * x] = v >> 8; p[2 * x + 1] = v; break;
    default: p[4 * x] = v >> 24; p[4 * x + 1] = v >> 16; p[4 * x + 2] = v >> 8; p[4 * x + 3] = v; break;
    }
}
static uint32_t depth_mask(int d) { return d >= 32 ? 0xFFFFFFFFu : ((1u << d) - 1); }

/* ---- color conversion ---- */
static RGB ct_entry(uint32_t ctab, uint32_t idx) {
    RGB c = {0, 0, 0}; if (!ctab) return c; uint32_t p = rd32(ctab); if (!p) return c;
    uint32_t size = rd16(p + 6); if (idx > size) return c;
    uint32_t e = p + 8 + idx * 8; c.r = rd16(e + 2); c.g = rd16(e + 4); c.b = rd16(e + 6); return c;
}
static RGB px_rgb(const Surf *s, uint32_t px) {
    RGB c;
    switch (s->depth) {
    case 1: c.r = c.g = c.b = px ? 0 : 0xFFFF; return c;
    case 2: case 4: case 8: return ct_entry(s->ctab, px);
    case 16: { uint32_t r = (px >> 10) & 31, g = (px >> 5) & 31, b = px & 31; c.r = r * 0x0842 + (r >> 4); c.g = g * 0x0842 + (g >> 4); c.b = b * 0x0842 + (b >> 4); c.r = r << 11 | r << 6 | r << 1 | r >> 4; c.g = g << 11 | g << 6 | g << 1 | g >> 4; c.b = b << 11 | b << 6 | b << 1 | b >> 4; return c; }
    default: c.r = ((px >> 16) & 255) * 0x101; c.g = ((px >> 8) & 255) * 0x101; c.b = (px & 255) * 0x101; return c;
    }
}
typedef struct { uint32_t ctab, ptr, seed, size; uint8_t inv[32768]; int valid; } ICache;
static ICache icache[4]; static int icnext = 0;
static uint8_t *inverse_table(uint32_t ctab) {
    uint32_t p = rd32(ctab), seed = rd32(p), size = rd16(p + 6);
    for (int i = 0; i < 4; i++) if (icache[i].valid && icache[i].ctab == ctab && icache[i].ptr == p && icache[i].seed == seed && icache[i].size == size) {
        /* verify table content unchanged cheaply: first/last entries */
        return icache[i].inv;
    }
    ICache *c = &icache[icnext++ & 3]; c->valid = 1; c->ctab = ctab; c->ptr = p; c->seed = seed; c->size = size;
    int n = size + 1; if (n > 256) n = 256; uint8_t rr[256], gg[256], bb[256];
    for (int i = 0; i < n; i++) { uint32_t e = p + 8 + i * 8; rr[i] = rd16(e + 2) >> 8; gg[i] = rd16(e + 4) >> 8; bb[i] = rd16(e + 6) >> 8; }
    for (int k = 0; k < 32768; k++) {
        int r = ((k >> 10) & 31) << 3 | 4, g = ((k >> 5) & 31) << 3 | 4, b = (k & 31) << 3 | 4; int best = 0, bd = 1 << 30;
        for (int i = 0; i < n; i++) { int dr = r - rr[i], dg = g - gg[i], db = b - bb[i]; int d = dr * dr * 2 + dg * dg * 4 + db * db; if (d < bd) { bd = d; best = i; if (!d) break; } }
        c->inv[k] = best;
    }
    return c->inv;
}
static uint32_t rgb_px(const Surf *s, RGB c) {
    switch (s->depth) {
    case 1: return ((c.r + c.g + c.b) / 3 < 0x8000) ? 1 : 0;
    case 8: if (!s->ctab) return 0; return inverse_table(s->ctab)[((c.r >> 11) << 10) | ((c.g >> 11) << 5) | (c.b >> 11)];
    case 2: case 4: { uint32_t p = rd32(s->ctab); int n = rd16(p + 6) + 1, best = 0; long bd = 1L << 40; for (int i = 0; i < n; i++) { RGB e = ct_entry(s->ctab, i); long dr = (long)c.r - e.r, dg = (long)c.g - e.g, db = (long)c.b - e.b; long d = dr * dr + dg * dg + db * db; if (d < bd) { bd = d; best = i; } } return best; }
    case 16: return ((c.r >> 11) << 10) | ((c.g >> 11) << 5) | (c.b >> 11);
    default: return ((c.r >> 8) << 16) | ((c.g >> 8) << 8) | (c.b >> 8);
    }
}
static RGB qd_color_const(uint32_t c) {
    RGB k = {0, 0, 0};
    switch (c) { case 30: k.r = k.g = k.b = 0xFFFF; break; case 33: break; case 205: k.r = 0xDD6B; k.g = 0x08C2; k.b = 0x06A2; break;
    case 341: k.g = 0x8000; k.b = 0x11B0; break; case 409: k.b = 0xD400; break; case 273: k.g = 0xAB92; k.b = 0xEAF4; break;
    case 137: k.r = 0xF2D7; k.g = 0x0856; k.b = 0x84EC; break; case 69: k.r = 0xFC00; k.g = 0xF37D; k.b = 0x052F; break; default: break; }
    return k;
}

/* ---- draw context ---- */
typedef struct {
    uint32_t port; Surf s; RL clip; uint32_t fg, bg;
    int mode; int ptype; uint8_t p8[8]; Surf psurf; int pw, ph;     /* pattern: p8 (1-bit) or psurf (pixel pattern) */
    int solid; uint32_t solid_px;
} DC;

static void dc_port_colors(uint32_t port, const Surf *s, uint32_t *fg, uint32_t *bg) {
    if (port_is_color(port)) {
        RGB f = {rd16(port + 36), rd16(port + 38), rd16(port + 40)}, b = {rd16(port + 42), rd16(port + 44), rd16(port + 46)};
        *fg = rgb_px(s, f); *bg = rgb_px(s, b);
    } else {
        *fg = rgb_px(s, qd_color_const(rd32(port + 80))); *bg = rgb_px(s, qd_color_const(rd32(port + 84)));
        if (s->depth == 1) { *fg = rd32(port + 80) == 30 ? 0 : 1; *bg = rd32(port + 84) == 33 ? 1 : 0; }
    }
}
static void dc_set_pat8(DC *d, const uint8_t *p) {
    memcpy(d->p8, p, 8); d->ptype = 0;
    int all1 = 1, all0 = 1; for (int i = 0; i < 8; i++) { if (p[i] != 0xFF) all1 = 0; if (p[i] != 0) all0 = 0; }
    d->solid = all1 || all0; d->solid_px = all1 ? d->fg : d->bg;
}
static void dc_set_pixpat(DC *d, uint32_t pph) {       /* PixPatHandle */
    uint32_t pp_ = pph ? rd32(pph) : 0; if (!pp_) { uint8_t b[8]; memset(b, 0xFF, 8); dc_set_pat8(d, b); return; }
    uint32_t type = rd16(pp_);
    if (type == 0) { uint8_t b[8]; for (int i = 0; i < 8; i++) b[i] = rd8(pp_ + 22 + i); dc_set_pat8(d, b); return; }
    if (type == 2) { /* RGB pattern: patXMap -> RGBColor */ RGB c = {rd16(rd32(pp_ + 18) ? rd32(rd32(pp_ + 18)) : 0), 0, 0}; (void)c; uint8_t b[8]; memset(b, 0xFF, 8); dc_set_pat8(d, b); return; }
    uint32_t pmh = rd32(pp_ + 2), dh = rd32(pp_ + 6); if (!pmh || !dh) { uint8_t b[8]; memset(b, 0xFF, 8); dc_set_pat8(d, b); return; }
    uint32_t pm = rd32(pmh); surf_from_pm(pm, &d->psurf); d->psurf.base = rd32(dh); d->psurf.color = 1;
    d->pw = d->psurf.bnd.r - d->psurf.bnd.l; d->ph = d->psurf.bnd.b - d->psurf.bnd.t; if (d->pw < 1) d->pw = 1; if (d->ph < 1) d->ph = 1;
    d->ptype = 1; d->solid = 0;
}
static void dc_pen_pat(DC *d) {
    if (port_is_color(d->port)) { uint32_t h = rd32(d->port + 58); if (h) { dc_set_pixpat(d, h); return; } }
    uint8_t b[8]; for (int i = 0; i < 8; i++) b[i] = rd8(d->port + 58 + i); dc_set_pat8(d, b);
}
static void dc_bk_pat(DC *d) {
    if (port_is_color(d->port)) { uint32_t h = rd32(d->port + 32); if (h) { dc_set_pixpat(d, h); return; } }
    uint8_t b[8]; for (int i = 0; i < 8; i++) b[i] = rd8(d->port + 32 + i); dc_set_pat8(d, b);
}
static void dc_init(DC *d, uint32_t port) {
    memset(d, 0, sizeof *d); d->port = port; surf_from_port(port, &d->s);
    dc_port_colors(port, &d->s, &d->fg, &d->bg);
    RL a = {0}, b = {0}; rgn_get(rd32(port + 28), &a); rgn_get(rd32(port + 24), &b); rl_sect(&a, &b, &d->clip);
    rl_free(&a); rl_free(&b);
    RL bounds = {0}, t2 = {0}; rl_add(&bounds, d->s.bnd); rl_sect(&d->clip, &bounds, &t2); rl_free(&d->clip); rl_free(&bounds); d->clip = t2;
    d->mode = (int16_t)rd16(port + 56); dc_pen_pat(d);
}
static void dc_free(DC *d) { rl_free(&d->clip); gfx_dirty = 1; }

static inline uint32_t dc_patpx(const DC *d, int x, int y) {
    if (d->ptype == 0) return (d->p8[y & 7] >> (7 - (x & 7)) & 1) ? d->fg : d->bg;
    int px = (x - d->psurf.bnd.l) % d->pw, py = (y - d->psurf.bnd.t) % d->ph; if (px < 0) px += d->pw; if (py < 0) py += d->ph;
    uint32_t v = gp(&d->psurf, d->psurf.bnd.l + px, d->psurf.bnd.t + py);
    if (d->psurf.depth == d->s.depth && d->psurf.ctab == d->s.ctab) return v;
    return rgb_px(&d->s, px_rgb(&d->psurf, v));
}
static inline uint32_t apply_mode(int mode, uint32_t src, uint32_t dst, uint32_t m) {
    switch (mode & 7) {
    case 0: return src; case 1: return dst | src; case 2: return dst ^ src; case 3: return dst & ~src;
    case 4: return ~src & m; case 5: return dst | (~src & m); case 6: return dst ^ (~src & m); default: return dst & ~(~src & m);
    }
}
static void dc_span(DC *d, int y, int x0, int x1) {          /* pattern fill with d->mode into [x0,x1) at row y, honoring clip */
    uint32_t m = depth_mask(d->s.depth); int md = d->mode & 7;
    for (int i = 0; i < d->clip.n; i++) {
        Rc c = d->clip.r[i]; if (y < c.t || y >= c.b) continue;
        int a = x0 > c.l ? x0 : c.l, b = x1 < c.r ? x1 : c.r; if (a >= b) continue;
        if (d->solid && d->s.depth == 8 && md == 0) { memset(ram + ((d->s.base + (uint32_t)(y - d->s.bnd.t) * d->s.rb + (a - d->s.bnd.l)) & 0xFFFFFF), d->solid_px, b - a); continue; }
        for (int x = a; x < b; x++) {
            uint32_t src = d->solid ? d->solid_px : dc_patpx(d, x, y);
            pp(&d->s, x, y, md == 0 ? src : apply_mode(d->mode, src, gp(&d->s, x, y), m));
        }
    }
}
static void dc_rect(DC *d, Rc r) { for (int y = r.t; y < r.b; y++) dc_span(d, y, r.l, r.r); }

/* ---- shapes ---- */
enum { SH_RECT = 0, SH_OVAL, SH_RRECT };
static int shape_span(int kind, Rc r, int ow, int oh, int y, int *x0, int *x1) {
    if (y < r.t || y >= r.b) return 0;
    if (kind == SH_RECT) { *x0 = r.l; *x1 = r.r; return r.r > r.l; }
    double w = r.r - r.l, h = r.b - r.t; if (w <= 0 || h <= 0) return 0;
    if (kind == SH_OVAL) {
        double cy = r.t + h / 2, b = h / 2, dy = (y + 0.5 - cy) / b; if (dy <= -1 || dy >= 1) return 0;
        double hw = (w / 2) * sqrt(1 - dy * dy), cx = r.l + w / 2; *x0 = (int)floor(cx - hw + 0.5); *x1 = (int)floor(cx + hw + 0.5);
        if (*x1 <= *x0) { *x0 = (int)floor(cx); *x1 = *x0 + 1; } return 1;
    }
    double a = ow / 2.0, b = oh / 2.0; if (a > w / 2) a = w / 2; if (b > h / 2) b = h / 2;
    double inset = 0;
    if (b > 0 && y + 0.5 < r.t + b) { double dy = (r.t + b - (y + 0.5)) / b; inset = a * (1 - sqrt(dy >= 1 ? 0 : 1 - dy * dy)); }
    else if (b > 0 && y + 0.5 > r.b - b) { double dy = ((y + 0.5) - (r.b - b)) / b; inset = a * (1 - sqrt(dy >= 1 ? 0 : 1 - dy * dy)); }
    *x0 = r.l + (int)floor(inset + 0.5); *x1 = r.r - (int)floor(inset + 0.5); return *x1 > *x0;
}
static void shape_paint(DC *d, int kind, Rc r, int ow, int oh) {
    for (int y = r.t; y < r.b; y++) { int a, b; if (shape_span(kind, r, ow, oh, y, &a, &b)) dc_span(d, y, a, b); }
}
static void shape_frame(DC *d, int kind, Rc r, int ow, int oh, int pw, int ph) {
    Rc in = {r.t + ph, r.l + pw, r.b - ph, r.r - pw};
    if (rc_empty(in)) { shape_paint(d, kind, r, ow, oh); return; }
    int iow = ow - 2 * pw > 0 ? ow - 2 * pw : 0, ioh = oh - 2 * ph > 0 ? oh - 2 * ph : 0;
    for (int y = r.t; y < r.b; y++) {
        int a, b; if (!shape_span(kind, r, ow, oh, y, &a, &b)) continue;
        int c, e;
        if (y >= in.t && y < in.b && shape_span(kind, in, iow, ioh, y, &c, &e) && c < e) { if (c > a) dc_span(d, y, a, c < b ? c : b); if (e < b) dc_span(d, y, e > a ? e : a, b); }
        else dc_span(d, y, a, b);
    }
}
static void shape_invert(DC *d, int kind, Rc r, int ow, int oh) {
    int sm = d->mode; uint8_t b[8]; memset(b, 0xFF, 8); uint32_t f = d->fg; d->fg = depth_mask(d->s.depth); dc_set_pat8(d, b); d->mode = 10; shape_paint(d, kind, r, ow, oh); d->mode = sm; d->fg = f;
}
/* arcs: angles in degrees, 0 = up, clockwise */
static int in_arc(Rc r, int x, int y, int start, int arc) {
    double cx = (r.l + r.r) / 2.0, cy = (r.t + r.b) / 2.0; double dx = x + 0.5 - cx, dy = cy - (y + 0.5);
    double ang = atan2(dx, dy) * 180.0 / M_PI; if (ang < 0) ang += 360;
    double s = fmod((double)start, 360); if (s < 0) s += 360; double rel = ang - s; if (arc >= 0) { while (rel < 0) rel += 360; return rel <= arc; }
    rel = s - ang; while (rel < 0) rel += 360; return rel <= -arc;
}
static void arc_paint(DC *d, Rc r, int start, int arc, int ring, int pw, int ph) {
    Rc in = {r.t + ph, r.l + pw, r.b - ph, r.r - pw};
    for (int y = r.t; y < r.b; y++) {
        int a, b; if (!shape_span(SH_OVAL, r, 0, 0, y, &a, &b)) continue;
        int c = 0, e = 0, hasin = ring && y >= in.t && y < in.b && shape_span(SH_OVAL, in, 0, 0, y, &c, &e);
        for (int x = a; x < b; x++) { if (hasin && x >= c && x < e) continue; if (!in_arc(r, x, y, start, arc)) continue; dc_span(d, y, x, x + 1); }
    }
}
/* polygons */
typedef struct { int n; int16_t *v, *h; Rc bb; } Poly;
static void poly_get(uint32_t ph, Poly *p) {
    uint32_t a = rd32(ph); int sz = rd16(a); p->n = (sz - 10) / 4; if (p->n < 0) p->n = 0; p->bb = rc_get(a + 2);
    p->v = malloc((p->n + 1) * 2); p->h = malloc((p->n + 1) * 2); for (int i = 0; i < p->n; i++) { p->v[i] = rd16(a + 10 + 4 * i); p->h[i] = rd16(a + 12 + 4 * i); }
}
static void poly_free(Poly *p) { free(p->v); free(p->h); }
static void poly_fill(DC *d, const Poly *p) {
    if (p->n < 3) return; int mn = 32767, mx = -32768; for (int i = 0; i < p->n; i++) { if (p->v[i] < mn) mn = p->v[i]; if (p->v[i] > mx) mx = p->v[i]; }
    int *xs = malloc(sizeof(int) * (p->n + 2));
    for (int y = mn; y < mx; y++) {
        int k = 0; double yc = y + 0.5;
        for (int i = 0; i < p->n; i++) { int j = (i + 1) % p->n; double y0 = p->v[i], y1 = p->v[j]; if ((y0 <= yc && y1 > yc) || (y1 <= yc && y0 > yc)) xs[k++] = (int)floor(p->h[i] + (yc - y0) * (p->h[j] - p->h[i]) / (y1 - y0) + 0.5); }
        for (int a = 1; a < k; a++) { int t = xs[a], b = a - 1; while (b >= 0 && xs[b] > t) { xs[b + 1] = xs[b]; b--; } xs[b + 1] = t; }
        for (int a = 0; a + 1 < k; a += 2) dc_span(d, y, xs[a], xs[a + 1]);
    }
    free(xs);
}
/* lines: pen rect stepped along Bresenham path */
static void pen_line(DC *d, int x0, int y0, int x1, int y1, int pw, int ph) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        Rc r = {y0, x0, y0 + ph, x0 + pw}; dc_rect(d, r);
        if (x0 == x1 && y0 == y1) break; int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
/* region mask drawing (rect list) */
static void rgn_paint(DC *d, uint32_t rgn) { RL l = {0}; rgn_get(rgn, &l); for (int i = 0; i < l.n; i++) dc_rect(d, l.r[i]); rl_free(&l); }
static void rgn_frame(DC *d, uint32_t rgn, int pw, int ph) {
    RL l = {0}; rgn_get(rgn, &l); if (!l.n) return;
    Rc bb = l.r[0]; for (int i = 1; i < l.n; i++) { if (l.r[i].t < bb.t) bb.t = l.r[i].t; if (l.r[i].l < bb.l) bb.l = l.r[i].l; if (l.r[i].b > bb.b) bb.b = l.r[i].b; if (l.r[i].r > bb.r) bb.r = l.r[i].r; }
    int w = bb.r - bb.l, h = bb.b - bb.t; if (w <= 0 || h <= 0 || w * h > 4000000) { rl_free(&l); return; }
    uint8_t *m = calloc(w * h, 1); for (int i = 0; i < l.n; i++) for (int y = l.r[i].t; y < l.r[i].b; y++) for (int x = l.r[i].l; x < l.r[i].r; x++) m[(y - bb.t) * w + x - bb.l] = 1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if (m[y * w + x]) {
        int inner = 1; for (int k = 1; k <= pw && inner; k++) if (x - k < 0 || !m[y * w + x - k] || x + k >= w || !m[y * w + x + k]) inner = 0;
        for (int k = 1; k <= ph && inner; k++) if (y - k < 0 || !m[(y - k) * w + x] || y + k >= h || !m[(y + k) * w + x]) inner = 0;
        if (!inner) dc_span(d, bb.t + y, bb.l + x, bb.l + x + 1);
    }
    free(m); rl_free(&l);
}

/* ---- text ---- */
static const FStrike *pick_strike(int font, int face, int size) {
    int fam = (font == 4 || font == 22) ? 2 : (font == 2 || font == 20 || font == 6 || font == 7 || font == 16) ? 1 : 0;
    if (size <= 0) size = 12; int bold = face & 1; const FStrike *best = NULL; int bd = 1 << 30;
    for (int i = 0; i < N_FSTRIKES; i++) { const FStrike *f = &font_strikes[i]; if (f->fam != fam || f->bold != bold) continue; int dd = abs(f->size - size) * 2 + (f->size > size ? 1 : 0); if (dd < bd) { bd = dd; best = f; } }
    return best;
}
static int text_extra(uint32_t port) { return (int32_t)rd32(port + 76) >> 16; }
static int str_width(uint32_t port, const uint8_t *s, int n) {
    const FStrike *f = pick_strike((int16_t)rd16(port + 68), rd8(port + 70), (int16_t)rd16(port + 74)); if (!f) return 0;
    int w = 0, ex = text_extra(port); for (int i = 0; i < n; i++) { w += f->g[s[i]].adv; if (s[i] == ' ') w += ex; }
    if (rd8(port + 70) & 0x10) w += 1; return w;
}
static void draw_text(DC *d, const uint8_t *s, int n) {
    uint32_t port = d->port; int font = (int16_t)rd16(port + 68), face = rd8(port + 70), size = (int16_t)rd16(port + 74), tmode = (int16_t)rd16(port + 72);
    const FStrike *f = pick_strike(font, face, size); int px = (int16_t)rd16(port + 50), py = (int16_t)rd16(port + 48);   /* pnLoc: v at +48, h at +50 */
    if (!f) return; int ex = text_extra(port), startx = px; int hidden = (int16_t)rd16(port + 66) < 0;
    uint32_t m = depth_mask(d->s.depth);
    for (int i = 0; i < n; i++) {
        const FGlyph *g = &f->g[s[i]];
        if (!hidden && (tmode & 7) == 0) { Rc cell = {py - f->ascent, px, py + f->descent, px + g->adv + (s[i] == ' ' ? ex : 0)}; uint8_t b[8]; memset(b, 0, 8); int sm = d->mode; uint32_t sf = d->fg; dc_set_pat8(d, b); d->mode = 8; dc_rect(d, cell); d->mode = sm; d->fg = sf; }
        if (!hidden && g->w > 0) {
            int rb = (g->w + 7) / 8;
            for (int gy = 0; gy < g->h; gy++) {
                int y = py - g->top + gy; int shear = (face & 2) ? (g->top - gy) / 4 : 0;
                for (int gx = 0; gx < g->w; gx++) if (font_blob[g->off + gy * rb + (gx >> 3)] & (0x80 >> (gx & 7))) {
                    int x = px + g->lb + gx + shear; int o = (face & 1) && 0;
                    (void)o;
                    for (int ci = 0; ci < d->clip.n; ci++) { Rc c = d->clip.r[ci]; if (x >= c.l && x < c.r && y >= c.t && y < c.b) {
                        uint32_t dst = gp(&d->s, x, y), v;
                        switch (tmode & 7) { case 0: case 1: v = d->fg; break; case 2: v = dst ^ m; break; case 3: v = d->bg; break; case 4: v = d->bg; break; default: v = d->fg; break; }
                        pp(&d->s, x, y, v); break; } }
                    if (face & 0x10) { /* shadow: offset copy */ int x2 = x + 1, y2 = y + 1; for (int ci = 0; ci < d->clip.n; ci++) { Rc c = d->clip.r[ci]; if (x2 >= c.l && x2 < c.r && y2 >= c.t && y2 < c.b) { pp(&d->s, x2, y2, d->fg); break; } } }
                }
            }
        }
        px += g->adv + (s[i] == ' ' ? ex : 0);
    }
    if (face & 4 && !hidden) { Rc r = {py + 1, startx, py + 2, px}; uint8_t b[8]; memset(b, 0xFF, 8); int sm = d->mode; dc_set_pat8(d, b); d->mode = 8; dc_rect(d, r); d->mode = sm; }
    wr16(port + 50, px);
}

/* ---- PackBits ---- */
static void unpack_bits(const uint8_t *src, int srclen, uint8_t *dst, int dstlen) {
    int si = 0, di = 0; while (si < srclen && di < dstlen) {
        int c = (int8_t)src[si++];
        if (c >= 0) { int n = c + 1; for (int i = 0; i < n && si < srclen && di < dstlen; i++) dst[di++] = src[si++]; }
        else if (c != -128) { int n = 1 - c; if (si >= srclen) break; uint8_t v = src[si++]; for (int i = 0; i < n && di < dstlen; i++) dst[di++] = v; }
    }
}
/* ---- CopyBits ---- */
static void rl_from_rgn(uint32_t rgn, RL *out) { if (rgn) rgn_get(rgn, out); }
static void copybits(const Surf *src, DC *d, Rc sr, Rc dr, int mode, uint32_t maskRgn) {
    if (rc_empty(dr) || rc_empty(sr)) return;
    RL clip = {0}, mrl = {0}, only = {0}; rl_add(&only, dr);
    if (maskRgn) { rl_from_rgn(maskRgn, &mrl); RL t = {0}; rl_sect(&d->clip, &mrl, &t); rl_sect(&t, &only, &clip); rl_free(&t); }
    else rl_sect(&d->clip, &only, &clip);
    rl_free(&mrl); rl_free(&only);
    int sw = sr.r - sr.l, sh = sr.b - sr.t, dw = dr.r - dr.l, dh = dr.b - dr.t;
    const Surf *ds = &d->s; uint32_t m = depth_mask(ds->depth);
    uint8_t xlat[256]; int use_xlat = 0;
    if (src->depth == 8 && ds->depth == 8 && src->ctab != ds->ctab) {
        int same = 1; uint32_t a = rd32(src->ctab), b = rd32(ds->ctab); int n = rd16(a + 6) + 1, n2 = rd16(b + 6) + 1;
        if (n != n2) same = 0; else for (int i = 0; i < n && same; i++) for (int k = 2; k < 8; k += 2) if ((rd16(a + 8 + i * 8 + k) >> 8) != (rd16(b + 8 + i * 8 + k) >> 8)) { same = 0; break; }
        if (!same) { use_xlat = 1; for (int i = 0; i < 256; i++) xlat[i] = rgb_px(ds, px_rgb(src, i)); }
    }
    int md = mode & 7; int arith = mode >= 32 && mode < 64;
    RGB bkrgb = port_is_color(d->port) ? (RGB){rd16(d->port + 42), rd16(d->port + 44), rd16(d->port + 46)} : qd_color_const(rd32(d->port + 84));
    for (int ci = 0; ci < clip.n; ci++) {
        Rc c = clip.r[ci];
        for (int y = c.t; y < c.b; y++) {
            int sy = sr.t + (int)((int64_t)(y - dr.t) * sh / dh); if (sy < src->bnd.t || sy >= src->bnd.b) continue;
            for (int x = c.l; x < c.r; x++) {
                int sx = sr.l + (int)((int64_t)(x - dr.l) * sw / dw); if (sx < src->bnd.l || sx >= src->bnd.r) continue;
                uint32_t sp_ = gp(src, sx, sy), v;
                if (src->depth == 1) v = sp_ ? d->fg : d->bg;
                else if (src->depth == ds->depth && (src->depth != 8 || !use_xlat)) v = sp_;
                else if (use_xlat) v = xlat[sp_ & 255];
                else v = rgb_px(ds, px_rgb(src, sp_));
                if (mode == 36) { RGB cc = px_rgb(ds, v); if (cc.r == bkrgb.r && cc.g == bkrgb.g && cc.b == bkrgb.b) continue; pp(ds, x, y, v); continue; }
                if (arith) { RGB sc = px_rgb(ds, v), dc = px_rgb(ds, gp(ds, x, y)), oc; uint32_t op = 0x8000;
                    if (port_is_color(d->port)) { uint32_t gv = rd32(d->port + 8); if (gv) { uint32_t gvp = rd32(gv); op = rd16(gvp); } }
                    int sc_[3] = {sc.r, sc.g, sc.b}, dc_[3] = {dc.r, dc.g, dc.b}, oc_[3], i;
                    for (i = 0; i < 3; i++) { int s_ = sc_[i], d_ = dc_[i], r_;
                        switch (mode) { case 32: r_ = (s_ * (int)op + d_ * (65535 - (int)op)) / 65535; break; case 33: r_ = s_ + d_; if (r_ > 65535) r_ = 65535; break; case 34: r_ = (s_ + d_) & 0xFFFF; break;
                        case 35: r_ = d_ - s_; if (r_ < 0) r_ = 0; break; case 37: r_ = s_ > d_ ? s_ : d_; break; case 38: r_ = (d_ - s_) & 0xFFFF; break; default: r_ = s_ < d_ ? s_ : d_; break; }
                        oc_[i] = r_; }
                    oc.r = oc_[0]; oc.g = oc_[1]; oc.b = oc_[2]; pp(ds, x, y, rgb_px(ds, oc)); continue; }
                pp(ds, x, y, md == 0 ? ((mode & 4) ? (~v & m) : v) : apply_mode(mode, v, gp(ds, x, y), m));
            }
        }
    }
    rl_free(&clip);
}

/* ---- PICT playback (PackBitsRect/BitsRect-centered subset + simple shape ops) ---- */
static void draw_pict(uint32_t pich, Rc dest) {
    uint32_t pic = rd32(pich); if (!pic) return;
    uint32_t sz = rd32(MP_BASE + 0) ? 0 : 0; (void)sz;
    uint32_t hs = hsize[slot_of(pich)]; Rc frame = rc_get(pic + 2); uint32_t p = pic + 10, end = pic + hs;
    int fw = frame.r - frame.l, fh = frame.b - frame.t; if (fw <= 0) fw = 1; if (fh <= 0) fh = 1;
    int dw = dest.r - dest.l, dh = dest.b - dest.t;
    uint32_t port = the_port(); DC d; dc_init(&d, port); int v2 = 0;
    if (rd16(p) == 0x0011 && rd16(p + 2) == 0x02FF) { v2 = 1; p += 4; } else if (rd8(p) == 0x11) p += 2;
    #define MAPX(x) (dest.l + (int)(((int64_t)((x) - frame.l)) * dw / fw))
    #define MAPY(y) (dest.t + (int)(((int64_t)((y) - frame.t)) * dh / fh))
    while (p + 2 <= end) {
        uint32_t op;
        if (v2) { if ((p - pic) & 1) p++; op = rd16(p); p += 2; } else { op = rd8(p); p += 1; }
        if (op == 0xFF) break;
        if (op == 0x0C00) { p += 24; continue; }
        if (op == 0x00 || op == 0x1E) continue;
        if (op == 0x01) { p += rd16(p); continue; }
        if (op == 0x03 || op == 0x04 || op == 0x08 || op == 0x0D || op == 0x15 || op == 0x16 || op == 0xA0) { p += 2; continue; }
        if (op == 0x07 || op == 0x21 || op == 0x22) { p += op == 0x22 ? 6 : 4; if (op == 0x21) p += 4; continue; }
        if (op == 0x1A || op == 0x1B || op == 0x1D || op == 0x1F || op == 0x1C) { if (op == 0x1A) { /* RGBFgCol */ } p += 6; continue; }
        if (op == 0x02 || op == 0x09 || op == 0x0A) { p += 8; continue; }
        if (op == 0xA1) { p += 2; p += 2 + rd16(p); continue; }
        if (op >= 0x30 && op <= 0x34) { Rc r = rc_get(p); p += 8; r = (Rc){MAPY(r.t), MAPX(r.l), MAPY(r.b), MAPX(r.r)}; int sm = d.mode; if (op == 0x31) { dc_rect(&d, r); } d.mode = sm; continue; }
        if (op == 0x98 || op == 0x99 || op == 0x90 || op == 0x91) {
            uint32_t rbw = rd16(p); Surf s; uint32_t pm = p; int packed = (op == 0x98 || op == 0x99);
            int ispix = (rbw & 0x8000) != 0; uint32_t rowb = rbw & 0x3FFF; Rc bnd = rc_get(p + 2); int depth = 1; uint32_t ctab = 0; uint32_t q = p;
            int pack = 0; Rc srcr, dstr; uint32_t maskh = 0;
            if (ispix) {
                pack = (int16_t)rd16(pm + 12); depth = rd16(pm + 28); int cmp = rd16(pm + 30); (void)cmp; q = pm + 46;
                uint32_t ctsize = rd16(q + 6) + 1; ctab = new_handle(8 + 8 * ctsize); uint32_t cp = rd32(ctab); wr32(cp, ++ctseed); wr16(cp + 6, ctsize - 1);
                for (uint32_t i = 0; i < ctsize; i++) { for (int k = 0; k < 8; k += 2) wr16(cp + 8 + i * 8 + k, rd16(q + 8 + i * 8 + k)); wr16(cp + 8 + i * 8, i); }
                q += 8 + ctsize * 8;
            } else q = p + 10;
            srcr = rc_get(q); dstr = rc_get(q + 8); int pmode = rd16(q + 16); q += 18;
            if (op == 0x99 || op == 0x91) { uint32_t rs = rd16(q); maskh = new_handle(rs); memcpy(ram + rd32(maskh), ram + q, rs); q += rs; }
            int h = bnd.b - bnd.t, w = bnd.r - bnd.l; uint32_t outrb = ((w * depth + 31) / 32) * 4; uint32_t buf = alloc_block(outrb * (h ? h : 1));
            if (!buf) { if (ctab) dispose_handle(ctab); break; }
            for (int y = 0; y < h; y++) {
                if (!packed || (pack == 1) || (rowb < 8)) { memcpy(ram + buf + y * outrb, ram + q, rowb < outrb ? rowb : outrb); q += rowb; }
                else {
                    uint32_t n; if (rowb > 250) { n = rd16(q); q += 2; } else { n = rd8(q); q += 1; }
                    if (depth == 16 && pack == 3) { uint8_t tmp[4096]; unpack_bits(ram + q, n, tmp, w * 2); memcpy(ram + buf + y * outrb, tmp, w * 2 < (int)outrb ? w * 2 : outrb); }
                    else unpack_bits(ram + q, n, ram + buf + y * outrb, outrb);
                    q += n;
                }
            }
            s.base = buf; s.rb = outrb; s.depth = depth; s.bnd = bnd; s.ctab = ctab; s.color = ispix;
            Rc ddr = {MAPY(dstr.t), MAPX(dstr.l), MAPY(dstr.b), MAPX(dstr.r)};
            if (depth == 1 && !ispix) { s.depth = 1; }
            copybits(&s, &d, srcr, ddr, pmode & 0x3F, maskh);
            free_block(buf); if (ctab) dispose_handle(ctab); if (maskh) dispose_handle(maskh);
            p = q; continue;
        }
        if (op == 0x9A || op == 0x9B) { /* DirectBitsRect: not used by game; skip rest */ fprintf(stderr, "  [pict] DirectBits not supported\n"); break; }
        if (op >= 0x100 && op < 0x8000) { p += (op >> 8) * 2; continue; }
        fprintf(stderr, "  [pict] unhandled op %04x at %06x\n", op, p); break;
    }
    dc_free(&d);
}
