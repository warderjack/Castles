/* ---- QuickDraw drawing trap dispatcher (uses gfx.inc) ---- */
static int pen_hidden(uint32_t port) { return (int16_t)rd16(port + 66) < 0; }
static void get_str(uint32_t p, uint8_t *buf, int *n) { *n = rd8(p); for (int i = 0; i < *n; i++) buf[i] = rd8(p + 1 + i); }
static void ensure_pixpat(uint32_t port, uint32_t fieldoff, const uint8_t *p8) {      /* make color port pattern field hold an old-style PixPat */
    if (!port_is_color(port)) { for (int i = 0; i < 8; i++) wr8(port + fieldoff + i, p8[i]); return; }
    uint32_t h = rd32(port + fieldoff); if (!h) { h = new_handle(28); wr32(port + fieldoff, h); }
    uint32_t a = rd32(h); wr16(a, 0); for (int i = 0; i < 8; i++) wr8(a + 22 + i, p8[i]);
}
static void set_fore_rgb(uint32_t port, RGB c) { if (port_is_color(port)) { wr16(port + 36, c.r); wr16(port + 38, c.g); wr16(port + 40, c.b); } else wr32(port + 80, (c.r + c.g + c.b) / 3 < 0x8000 ? 33 : 30); }
static void set_back_rgb(uint32_t port, RGB c) { if (port_is_color(port)) { wr16(port + 42, c.r); wr16(port + 44, c.g); wr16(port + 46, c.b); } else wr32(port + 84, (c.r + c.g + c.b) / 3 < 0x8000 ? 33 : 30); }
static int shape_op(DC *d, int which, int kind, Rc r, int ow, int oh) {      /* which: 0 frame,1 paint,2 erase,3 invert */
    uint32_t port = d->port; int pw = (int16_t)rd16(port + 54), ph = (int16_t)rd16(port + 52); if (pw < 1) pw = 1; if (ph < 1) ph = 1;
    switch (which) {
    case 0: if (!pen_hidden(port)) shape_frame(d, kind, r, ow, oh, pw, ph); break;
    case 1: if (!pen_hidden(port)) shape_paint(d, kind, r, ow, oh); break;
    case 2: dc_bk_pat(d); d->mode = 8; shape_paint(d, kind, r, ow, oh); break;
    default: shape_invert(d, kind, r, ow, oh); break;
    }
    return 1;
}
static void fill_pat_op(DC *d, uint32_t pat) { uint8_t b[8]; for (int i = 0; i < 8; i++) b[i] = rd8(pat + i); dc_set_pat8(d, b); d->mode = 8; }
static void do_scroll(uint32_t rp, int dh, int dv, uint32_t updRgn) {
    uint32_t port = the_port(); DC d; dc_init(&d, port); Rc r = rc_get(rp); Rc cr = r;
    int w = r.r - r.l, h = r.b - r.t; if (w <= 0 || h <= 0) { dc_free(&d); return; }
    uint32_t *tmp = malloc(sizeof(uint32_t) * w * h);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) tmp[y * w + x] = gp(&d.s, r.l + x, r.t + y);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int dx = x + dh, dy = y + dv; if (dx < 0 || dx >= w || dy < 0 || dy >= h) continue;
        int X = r.l + dx, Y = r.t + dy, ok = 0; for (int i = 0; i < d.clip.n; i++) { Rc c = d.clip.r[i]; if (X >= c.l && X < c.r && Y >= c.t && Y < c.b) { ok = 1; break; } }
        if (ok) pp(&d.s, X, Y, tmp[y * w + x]);
    }
    free(tmp);
    RL vac = {0}, whole = {0}, kept = {0}; rl_add(&whole, cr); Rc k = {r.t + (dv > 0 ? dv : 0), r.l + (dh > 0 ? dh : 0), r.b + (dv < 0 ? dv : 0), r.r + (dh < 0 ? dh : 0)}; rl_add(&kept, k); rl_diff(&whole, &kept, &vac);
    dc_bk_pat(&d); d.mode = 8; for (int i = 0; i < vac.n; i++) dc_rect(&d, vac.r[i]);
    if (updRgn) { RL u = {0}, t = {0}; rgn_get(updRgn, &u); rl_union(&u, &vac, &t); rgn_set(updRgn, &t); rl_free(&u); rl_free(&t); }
    rl_free(&vac); rl_free(&whole); rl_free(&kept); dc_free(&d);
}
static int gfx_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    uint32_t sp = SP; uint32_t port = the_port();
    if (n < 0xA800) return 0;
    #define NEEDPORT() if (!port) { if (trace_limit > 0) fprintf(stderr, "  [gfx] %04X with no port\n", n); }
    switch (n) {
    /* ---- rect / oval / roundrect: frame paint erase invert (4-byte arg) ---- */
    case 0xA8A1: case 0xA8A2: case 0xA8A3: case 0xA8A4: case 0xA8B7: case 0xA8B8: case 0xA8B9: case 0xA8BA: {
        int rect = n < 0xA8B0; int which = rect ? n - 0xA8A1 : n - 0xA8B7; Rc r = rc_get(rd32(sp)); setsp(sp + 4); if (!port) return 1;
        DC d; dc_init(&d, port); shape_op(&d, which, rect ? SH_RECT : SH_OVAL, r, 0, 0); dc_free(&d); return 1; }
    case 0xA8A5: case 0xA8BB: {   /* FillRect / FillOval(r, pat) */
        uint32_t pat = rd32(sp); Rc r = rc_get(rd32(sp + 4)); setsp(sp + 8); if (!port) return 1;
        DC d; dc_init(&d, port); fill_pat_op(&d, pat); shape_paint(&d, n == 0xA8A5 ? SH_RECT : SH_OVAL, r, 0, 0); dc_free(&d); return 1; }
    case 0xA8B0: case 0xA8B1: case 0xA8B2: case 0xA8B3: {
        int oh = (int16_t)rd16(sp), ow = (int16_t)rd16(sp + 2); Rc r = rc_get(rd32(sp + 4)); setsp(sp + 8); if (!port) return 1;
        DC d; dc_init(&d, port); shape_op(&d, n - 0xA8B0, SH_RRECT, r, ow, oh); dc_free(&d); return 1; }
    case 0xA8B4: {   /* FillRoundRect(r, ovalW, ovalH, pat) */
        uint32_t pat = rd32(sp); int oh = (int16_t)rd16(sp + 4), ow = (int16_t)rd16(sp + 6); Rc r = rc_get(rd32(sp + 8)); setsp(sp + 12); if (!port) return 1;
        DC d; dc_init(&d, port); fill_pat_op(&d, pat); shape_paint(&d, SH_RRECT, r, ow, oh); dc_free(&d); return 1; }
    case 0xA8BE: case 0xA8BF: case 0xA8C0: case 0xA8C1: {   /* Frame/Paint/Erase/InvertArc(r, start, arc) */
        int arc = (int16_t)rd16(sp), st = (int16_t)rd16(sp + 2); Rc r = rc_get(rd32(sp + 4)); setsp(sp + 8); if (!port) return 1;
        DC d; dc_init(&d, port); int which = n - 0xA8BE; int pw = (int16_t)rd16(port + 54), ph = (int16_t)rd16(port + 52);
        if (which == 0) { if (!pen_hidden(port)) arc_paint(&d, r, st, arc, 1, pw < 1 ? 1 : pw, ph < 1 ? 1 : ph); }
        else if (which == 1) { if (!pen_hidden(port)) arc_paint(&d, r, st, arc, 0, 1, 1); }
        else if (which == 2) { dc_bk_pat(&d); d.mode = 8; arc_paint(&d, r, st, arc, 0, 1, 1); }
        else { uint8_t b[8]; memset(b, 0xFF, 8); d.fg = depth_mask(d.s.depth); dc_set_pat8(&d, b); d.mode = 10; arc_paint(&d, r, st, arc, 0, 1, 1); }
        dc_free(&d); return 1; }
    case 0xA8C2: {   /* FillArc(r, start, arc, pat) */
        uint32_t pat = rd32(sp); int arc = (int16_t)rd16(sp + 4), st = (int16_t)rd16(sp + 6); Rc r = rc_get(rd32(sp + 8)); setsp(sp + 12); if (!port) return 1;
        DC d; dc_init(&d, port); fill_pat_op(&d, pat); arc_paint(&d, r, st, arc, 0, 1, 1); dc_free(&d); return 1; }
    /* ---- polygons ---- */
    case 0xA8C6: case 0xA8C7: case 0xA8C8: case 0xA8C9: {
        uint32_t ph = rd32(sp); setsp(sp + 4); if (!port) return 1; Poly p; poly_get(ph, &p); DC d; dc_init(&d, port); int which = n - 0xA8C6;
        if (which == 0) { if (!pen_hidden(port)) { int pw = (int16_t)rd16(port + 54), pht = (int16_t)rd16(port + 52); for (int i = 0; i + 1 < p.n; i++) pen_line(&d, p.h[i], p.v[i], p.h[i + 1], p.v[i + 1], pw, pht); } }
        else if (which == 1) { if (!pen_hidden(port)) poly_fill(&d, &p); }
        else if (which == 2) { dc_bk_pat(&d); d.mode = 8; poly_fill(&d, &p); }
        else { uint8_t b[8]; memset(b, 0xFF, 8); d.fg = depth_mask(d.s.depth); dc_set_pat8(&d, b); d.mode = 10; poly_fill(&d, &p); }
        poly_free(&p); dc_free(&d); return 1; }
    case 0xA8CA: { uint32_t pat = rd32(sp), ph = rd32(sp + 4); setsp(sp + 8); if (!port) return 1; Poly p; poly_get(ph, &p); DC d; dc_init(&d, port); fill_pat_op(&d, pat); poly_fill(&d, &p); poly_free(&p); dc_free(&d); return 1; }
    /* ---- regions ---- */
    case 0xA8D2: case 0xA8D3: case 0xA8D4: case 0xA8D5: {
        uint32_t rg = rd32(sp); setsp(sp + 4); if (!port) return 1; DC d; dc_init(&d, port); int which = n - 0xA8D2;
        if (which == 0) { if (!pen_hidden(port)) { int pw = (int16_t)rd16(port + 54), ph = (int16_t)rd16(port + 52); rgn_frame(&d, rg, pw < 1 ? 1 : pw, ph < 1 ? 1 : ph); } }
        else if (which == 1) { if (!pen_hidden(port)) rgn_paint(&d, rg); }
        else if (which == 2) { dc_bk_pat(&d); d.mode = 8; rgn_paint(&d, rg); }
        else { uint8_t b[8]; memset(b, 0xFF, 8); d.fg = depth_mask(d.s.depth); dc_set_pat8(&d, b); d.mode = 10; rgn_paint(&d, rg); }
        dc_free(&d); return 1; }
    case 0xA8D6: { uint32_t pat = rd32(sp), rg = rd32(sp + 4); setsp(sp + 8); if (!port) return 1; DC d; dc_init(&d, port); fill_pat_op(&d, pat); rgn_paint(&d, rg); dc_free(&d); return 1; }
    /* ---- pen ---- */
    case 0xA893: { int v = (int16_t)rd16(sp), h = (int16_t)rd16(sp + 2); setsp(sp + 4); if (port) { wr16(port + 48, v); wr16(port + 50, h); } return 1; }                       /* MoveTo */
    case 0xA894: { int dv = (int16_t)rd16(sp), dh = (int16_t)rd16(sp + 2); setsp(sp + 4); if (port) { wr16(port + 48, (int16_t)rd16(port + 48) + dv); wr16(port + 50, (int16_t)rd16(port + 50) + dh); } return 1; } /* Move */
    case 0xA891: case 0xA892: {      /* LineTo(h,v) / Line(dh,dv) */
        int v = (int16_t)rd16(sp), h = (int16_t)rd16(sp + 2); setsp(sp + 4); if (!port) return 1;
        int x0 = (int16_t)rd16(port + 50), y0 = (int16_t)rd16(port + 48); int x1 = n == 0xA891 ? h : x0 + h, y1 = n == 0xA891 ? v : y0 + v;
        if (!pen_hidden(port)) { DC d; dc_init(&d, port); int pw = (int16_t)rd16(port + 54), ph = (int16_t)rd16(port + 52); pen_line(&d, x0, y0, x1, y1, pw < 1 ? 1 : pw, ph < 1 ? 1 : ph); dc_free(&d); }
        wr16(port + 48, y1); wr16(port + 50, x1); return 1; }
    case 0xA89B: { int h = (int16_t)rd16(sp), w_ = (int16_t)rd16(sp + 2); setsp(sp + 4); if (port) { wr16(port + 52, h); wr16(port + 54, w_); } return 1; }     /* PenSize(width,height) */
    case 0xA89C: { int m = rd16(sp); setsp(sp + 2); if (port) wr16(port + 56, m); return 1; }                                                                            /* PenMode */
    case 0xA89D: { uint32_t p = rd32(sp); setsp(sp + 4); if (port) { uint8_t b[8]; for (int i = 0; i < 8; i++) b[i] = rd8(p + i); ensure_pixpat(port, 58, b); } return 1; }   /* PenPat */
    case 0xA87C: { uint32_t p = rd32(sp); setsp(sp + 4); if (port) { uint8_t b[8]; for (int i = 0; i < 8; i++) b[i] = rd8(p + i); ensure_pixpat(port, 32, b); } return 1; }   /* BackPat */
    case 0xA89E: if (port) { wr32(port + 52, 0x00010001); wr16(port + 56, 8); uint8_t b[8]; memset(b, 0xFF, 8); ensure_pixpat(port, 58, b); } return 1;                  /* PenNormal */
    case 0xA896: if (port) wr16(port + 66, (int16_t)rd16(port + 66) - 1); return 1;                                                                                     /* HidePen */
    case 0xA897: if (port) wr16(port + 66, (int16_t)rd16(port + 66) + 1); return 1;                                                                                     /* ShowPen */
    case 0xA89A: { uint32_t p = rd32(sp); setsp(sp + 4); if (port) { wr16(p, (int16_t)rd16(port + 48)); wr16(p + 2, (int16_t)rd16(port + 50)); } return 1; }                /* GetPen */
    case 0xA898: { uint32_t p = rd32(sp); setsp(sp + 4); if (port) { wr32(p, rd32(port + 48)); wr32(p + 4, rd32(port + 52)); wr16(p + 8, rd16(port + 56)); for (int i = 0; i < 8; i++) wr8(p + 10 + i, port_is_color(port) ? 0xFF : rd8(port + 58 + i)); } return 1; }  /* GetPenState */
    case 0xA899: { uint32_t p = rd32(sp); setsp(sp + 4); if (port) { wr32(port + 48, rd32(p)); wr32(port + 52, rd32(p + 4)); wr16(port + 56, rd16(p + 8)); uint8_t b[8]; for (int i = 0; i < 8; i++) b[i] = rd8(p + 10 + i); ensure_pixpat(port, 58, b); } return 1; }  /* SetPenState */
    /* ---- text ---- */
    case 0xA887: { int f = rd16(sp); setsp(sp + 2); if (port) wr16(port + 68, f); return 1; }
    case 0xA888: { int f = rd16(sp) & 0xFF; setsp(sp + 2); if (port) wr8(port + 70, f); return 1; }
    case 0xA889: { int m = rd16(sp); setsp(sp + 2); if (port) wr16(port + 72, m); return 1; }
    case 0xA88A: { int sz = rd16(sp); setsp(sp + 2); if (port) wr16(port + 74, sz); return 1; }
    case 0xA88E: { uint32_t e = rd32(sp); setsp(sp + 4); if (port) wr32(port + 76, e); return 1; }
    case 0xA88B: { uint32_t p = rd32(sp); setsp(sp + 4); const FStrike *f = port ? pick_strike((int16_t)rd16(port + 68), rd8(port + 70), (int16_t)rd16(port + 74)) : NULL;
        wr16(p, f ? f->ascent : 9); wr16(p + 2, f ? f->descent : 3); wr16(p + 4, f ? f->size : 12); wr16(p + 6, 0); return 1; }
    case 0xA883: { uint8_t c = rd16(sp) & 0xFF; setsp(sp + 2); if (port) { DC d; dc_init(&d, port); draw_text(&d, &c, 1); dc_free(&d); } return 1; }
    case 0xA884: { uint32_t p = rd32(sp); setsp(sp + 4); if (port) { uint8_t b[256]; int l; get_str(p, b, &l); DC d; dc_init(&d, port); draw_text(&d, b, l); dc_free(&d); } return 1; }
    case 0xA885: { int cnt = rd16(sp), first = rd16(sp + 2); uint32_t buf = rd32(sp + 4); setsp(sp + 8); if (port && cnt > 0) { DC d; dc_init(&d, port); draw_text(&d, ram + ((buf + first) & 0xFFFFFF), cnt); dc_free(&d); } return 1; }
    case 0xA886: { int cnt = rd16(sp), first = rd16(sp + 2); uint32_t buf = rd32(sp + 4); setsp(sp + 8); wr16(sp + 8, port && cnt > 0 ? str_width(port, ram + ((buf + first) & 0xFFFFFF), cnt) : 0); return 1; }
    case 0xA88C: { uint32_t p = rd32(sp); setsp(sp + 4); uint8_t b[256]; int l; get_str(p, b, &l); wr16(sp + 4, port ? str_width(port, b, l) : 0); return 1; }
    case 0xA88D: { uint8_t c = rd16(sp) & 0xFF; setsp(sp + 2); wr16(sp + 2, port ? str_width(port, &c, 1) : 0); return 1; }
    /* ---- colors ---- */
    case 0xA862: { uint32_t c = rd32(sp); setsp(sp + 4); if (port) { if (port_is_color(port)) set_fore_rgb(port, qd_color_const(c)); wr32(port + 80, c); } return 1; }
    case 0xA863: { uint32_t c = rd32(sp); setsp(sp + 4); if (port) { if (port_is_color(port)) set_back_rgb(port, qd_color_const(c)); wr32(port + 84, c); } return 1; }
    case 0xAA14: { uint32_t p = rd32(sp); setsp(sp + 4); if (port) { RGB c = {rd16(p), rd16(p + 2), rd16(p + 4)}; set_fore_rgb(port, c); } return 1; }
    case 0xAA15: { uint32_t p = rd32(sp); setsp(sp + 4); if (port) { RGB c = {rd16(p), rd16(p + 2), rd16(p + 4)}; set_back_rgb(port, c); } return 1; }
    case 0xAA19: case 0xAA1A: { uint32_t p = rd32(sp); setsp(sp + 4); if (port) { uint32_t o = n == 0xAA19 ? 36 : 42; if (port_is_color(port)) { wr16(p, rd16(port + o)); wr16(p + 2, rd16(port + o + 2)); wr16(p + 4, rd16(port + o + 4)); } else { RGB c = qd_color_const(rd32(port + (n == 0xAA19 ? 80 : 84))); wr16(p, c.r); wr16(p + 2, c.g); wr16(p + 4, c.b); } } return 1; }
    case 0xAA21: case 0xAA22: { uint32_t p = rd32(sp); setsp(sp + 4); (void)p; return 1; }       /* OpColor / HiliteColor (opColor tracked per-port lazily) */
    case 0xAA16: { uint32_t p = rd32(sp); int v = (int16_t)rd16(sp + 4), h = (int16_t)rd16(sp + 6); setsp(sp + 8); if (port) { Surf s; surf_from_port(port, &s); RGB c = {rd16(p), rd16(p + 2), rd16(p + 4)}; if (h >= s.bnd.l && h < s.bnd.r && v >= s.bnd.t && v < s.bnd.b) pp(&s, h, v, rgb_px(&s, c)); gfx_dirty = 1; } return 1; }  /* SetCPixel(h,v,RGBColor*) */
    case 0xAA17: { uint32_t p = rd32(sp); int v = (int16_t)rd16(sp + 4), h = (int16_t)rd16(sp + 6); setsp(sp + 8); if (port) { Surf s; surf_from_port(port, &s); RGB c = {0, 0, 0}; if (h >= s.bnd.l && h < s.bnd.r && v >= s.bnd.t && v < s.bnd.b) c = px_rgb(&s, gp(&s, h, v)); wr16(p, c.r); wr16(p + 2, c.g); wr16(p + 4, c.b); } return 1; }
    case 0xA865: { int v = (int16_t)rd16(sp), h = (int16_t)rd16(sp + 2); setsp(sp + 4); int bl = 0; if (port) { Surf s; surf_from_port(port, &s); if (h >= s.bnd.l && h < s.bnd.r && v >= s.bnd.t && v < s.bnd.b) { RGB c = px_rgb(&s, gp(&s, h, v)); bl = (c.r + c.g + c.b) / 3 < 0x8000; } } wr_bool(sp + 4, bl); return 1; }  /* GetPixel */
    case 0xAA30: { uint32_t p = rd32(sp); setsp(sp + 4); Surf s; memset(&s, 0, sizeof s); s.depth = 8; s.ctab = main_ct; RGB c = {rd16(p), rd16(p + 2), rd16(p + 4)}; wr32(sp + 4, rgb_px(&s, c)); return 1; }  /* Color2Index(RGB*): long */
    case 0xAA33: { uint32_t p = rd32(sp); uint32_t idx = rd32(sp + 4); setsp(sp + 8); RGB c = ct_entry(main_ct, idx); wr16(p, c.r); wr16(p + 2, c.g); wr16(p + 4, c.b); return 1; }                    /* Index2Color(index, RGB*) */
    case 0xAA0E: { uint32_t ph = rd32(sp); Rc r = rc_get(rd32(sp + 4)); setsp(sp + 8); if (port) { DC d; dc_init(&d, port); dc_set_pixpat(&d, ph); d.mode = 8; shape_paint(&d, SH_RECT, r, 0, 0); dc_free(&d); } return 1; }   /* FillCRect */
    case 0xAA0F: { uint32_t ph = rd32(sp); Rc r = rc_get(rd32(sp + 4)); setsp(sp + 8); if (port) { DC d; dc_init(&d, port); dc_set_pixpat(&d, ph); d.mode = 8; shape_paint(&d, SH_OVAL, r, 0, 0); dc_free(&d); } return 1; }   /* FillCOval */
    case 0xAA12: { uint32_t ph = rd32(sp), rg = rd32(sp + 4); setsp(sp + 8); if (port) { DC d; dc_init(&d, port); dc_set_pixpat(&d, ph); d.mode = 8; rgn_paint(&d, rg); dc_free(&d); } return 1; }                  /* FillCRgn */
    /* ---- bitmaps / pictures ---- */
    case 0xA8EC: {      /* CopyBits(src, dst, srcRect, dstRect, mode, maskRgn) */
        uint32_t mask = rd32(sp); int mode = rd16(sp + 4); Rc dr = rc_get(rd32(sp + 6)), sr = rc_get(rd32(sp + 10)); uint32_t dst = rd32(sp + 14), src = rd32(sp + 18); setsp(sp + 22);
        Surf ss, ds; surf_from_pm(src, &ss); surf_from_pm(dst, &ds); if (!port) return 1;
        DC d; dc_init(&d, port);
        if (ds.base != d.s.base || ds.rb != d.s.rb) { rl_free(&d.clip); d.clip = (RL){0}; rl_add(&d.clip, ds.bnd); }
        d.s = ds; dc_port_colors(port, &ds, &d.fg, &d.bg);
        copybits(&ss, &d, sr, dr, mode, mask); dc_free(&d); return 1; }
    case 0xA817: {      /* CopyMask(srcBits, maskBits, dstBits, srcRect, maskRect, dstRect) */
        Rc dr = rc_get(rd32(sp)), mr = rc_get(rd32(sp + 4)), sr = rc_get(rd32(sp + 8)); uint32_t dst = rd32(sp + 12), mask = rd32(sp + 16), src = rd32(sp + 20); setsp(sp + 24);
        Surf ss, ms, ds; surf_from_pm(src, &ss); surf_from_pm(mask, &ms); surf_from_pm(dst, &ds); if (!port) return 1;
        DC d; dc_init(&d, port); if (ds.base != d.s.base) { rl_free(&d.clip); d.clip = (RL){0}; rl_add(&d.clip, ds.bnd); } d.s = ds; dc_port_colors(port, &ds, &d.fg, &d.bg);
        int sw = sr.r - sr.l, sh = sr.b - sr.t, dw = dr.r - dr.l, dh = dr.b - dr.t, mw = mr.r - mr.l, mh = mr.b - mr.t;
        if (dw > 0 && dh > 0) for (int y = dr.t; y < dr.b; y++) for (int x = dr.l; x < dr.r; x++) {
            int ok = 0; for (int i = 0; i < d.clip.n; i++) { Rc c = d.clip.r[i]; if (x >= c.l && x < c.r && y >= c.t && y < c.b) { ok = 1; break; } } if (!ok) continue;
            int mx = mr.l + (int)((int64_t)(x - dr.l) * mw / dw), my = mr.t + (int)((int64_t)(y - dr.t) * mh / dh);
            if (mx < ms.bnd.l || mx >= ms.bnd.r || my < ms.bnd.t || my >= ms.bnd.b) continue; if (!gp(&ms, mx, my)) continue;
            int sx = sr.l + (int)((int64_t)(x - dr.l) * sw / dw), sy = sr.t + (int)((int64_t)(y - dr.t) * sh / dh);
            if (sx < ss.bnd.l || sx >= ss.bnd.r || sy < ss.bnd.t || sy >= ss.bnd.b) continue;
            uint32_t v = gp(&ss, sx, sy); if (ss.depth != ds.depth || (ss.depth == 8 && ss.ctab != ds.ctab)) v = rgb_px(&ds, px_rgb(&ss, v)); pp(&ds, x, y, v); }
        dc_free(&d); return 1; }
    case 0xA8EF: { uint32_t upd = rd32(sp); int dv = (int16_t)rd16(sp + 4), dh = (int16_t)rd16(sp + 6); uint32_t rp = rd32(sp + 8); setsp(sp + 12); if (port) do_scroll(rp, dh, dv, upd); return 1; }   /* ScrollRect */
    case 0xA8F6: { uint32_t rp = rd32(sp); uint32_t ph = rd32(sp + 4); setsp(sp + 8); if (port && ph && rd32(ph)) draw_pict(ph, rc_get(rp)); return 1; }          /* DrawPicture(pic, rect*) */
    case 0xA8F5: { uint32_t ph = rd32(sp); setsp(sp + 4); dispose_handle(ph); return 1; }                                                                          /* KillPicture */
    case 0xA875: { uint32_t bm = rd32(sp); setsp(sp + 4); if (port) memcpy(ram + port + 2, ram + bm, 14); return 1; }                                               /* SetPortBits */
    case 0xA94B: {      /* PlotIcon(rect*, iconHandle) -- 32x32 1-bit icon */
        uint32_t ih = rd32(sp); Rc r = rc_get(rd32(sp + 4)); setsp(sp + 8);
        if (port && ih && rd32(ih)) { Surf s; s.base = rd32(ih); s.rb = 4; s.depth = 1; s.bnd = (Rc){0, 0, 32, 32}; s.ctab = 0; s.color = 0; DC d; dc_init(&d, port); copybits(&s, &d, s.bnd, r, 1, 0); dc_free(&d); }
        return 1; }
    default: return 0;
    }
}
