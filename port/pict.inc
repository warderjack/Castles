/* ---- QuickDraw pictures: OpenPicture/ClosePicture recording (PICT v2 opcodes) and DrawPicture playback ---- */
static struct { uint32_t handle; Rc frame; uint8_t *b; int n, cap; } prc;
static void pe8(int v) { if (prc.n + 1 > prc.cap) { prc.cap = prc.cap ? prc.cap * 2 : 1024; prc.b = realloc(prc.b, prc.cap); } prc.b[prc.n++] = v; }
static void pe16(int v) { pe8((v >> 8) & 0xFF); pe8(v & 0xFF); }
static void pe32(uint32_t v) { pe16(v >> 16); pe16(v & 0xFFFF); }
static void pe_rect(Rc r) { pe16(r.t); pe16(r.l); pe16(r.b); pe16(r.r); }
static void pe_pad(void) { if (prc.n & 1) pe8(0); }
static void pe_op(int op) { pe_pad(); pe16(op); }
static void pe_bytes(uint32_t a, int n) { for (int i = 0; i < n; i++) pe8(rd8(a + i)); }

static uint32_t pic_open(Rc frame) {
    memset(&prc, 0, sizeof prc); pic_on = 1; prc.frame = frame; pe16(0); pe_rect(frame);       /* picSize + picFrame */
    pe16(0x0011); pe16(0x02FF); pe16(0x0C00); pe16(0xFFFE); pe16(0); pe32(0x00480000); pe32(0x00480000); pe_rect(frame); pe32(0); pe16(0x001E);
    prc.handle = new_handle(10); return prc.handle;
}
static void pic_close(void) {
    pe_op(0x00FF); pic_on = 0; uint32_t ph = prc.handle;
    set_handle_size(ph, prc.n); memcpy(ram + rd32(ph), prc.b, prc.n); wr16(rd32(ph), prc.n & 0xFFFF); free(prc.b); prc.b = NULL; prc.n = prc.cap = 0;
}
static void pic_pnloc_text(uint32_t port, const uint8_t *s, int n) {      /* LongText at pnLoc */
    pe_op(0x0028); pe16((int16_t)rd16(port + 48)); pe16((int16_t)rd16(port + 50)); pe8(n); for (int i = 0; i < n; i++) pe8(s[i]);
    int w = str_width(port, s, n); wr16(port + 50, (int16_t)rd16(port + 50) + w);
}
/* Called for every trap while a picture is open. Returns 1 when the trap was recorded and fully handled (drawing is not rendered),
   0 when the normal handler should also run (state changes apply to the port as well). */
static int pic_record(uint16_t n, uint32_t sp) {
    uint32_t port = the_port(); if (!port) return 0;
    switch (n) {
    case 0xA89B: pe_op(0x0007); pe16((int16_t)rd16(sp)); pe16((int16_t)rd16(sp + 2)); return 0;
    case 0xA89C: pe_op(0x0008); pe16(rd16(sp)); return 0;
    case 0xA89D: pe_op(0x0009); pe_bytes(rd32(sp), 8); return 0;
    case 0xA87C: pe_op(0x0002); pe_bytes(rd32(sp), 8); return 0;
    case 0xA89E: pe_op(0x0007); pe32(0x00010001); pe_op(0x0008); pe16(8); pe_op(0x0009); for (int i = 0; i < 8; i++) pe8(0xFF); return 0;
    case 0xA862: pe_op(0x000E); pe32(rd32(sp)); return 0;
    case 0xA863: pe_op(0x000F); pe32(rd32(sp)); return 0;
    case 0xAA14: pe_op(0x001A); pe_bytes(rd32(sp), 6); return 0;
    case 0xAA15: pe_op(0x001B); pe_bytes(rd32(sp), 6); return 0;
    case 0xAA21: pe_op(0x001F); pe_bytes(rd32(sp), 6); return 0;
    case 0xAA22: pe_op(0x001D); pe_bytes(rd32(sp), 6); return 0;
    case 0xA887: pe_op(0x0003); pe16(rd16(sp)); return 0;
    case 0xA888: pe_op(0x0004); pe8(rd16(sp) & 0xFF); pe_pad(); return 0;
    case 0xA889: pe_op(0x0005); pe16(rd16(sp)); return 0;
    case 0xA88A: pe_op(0x000D); pe16(rd16(sp)); return 0;
    case 0xA88E: pe_op(0x0006); pe32(rd32(sp)); return 0;
    case 0xA87B: { Rc r = rc_get(rd32(sp)); pe_op(0x0001); pe16(10); pe_rect(r); return 0; }
    case 0xA879: { uint32_t rg = rd32(sp); if (rg && rd32(rg)) { int sz = rd16(rd32(rg)); pe_op(0x0001); pe_bytes(rd32(rg), sz); } return 0; }
    case 0xA891: case 0xA892: { int h = (int16_t)rd16(sp + 2), v = (int16_t)rd16(sp); setsp(sp + 4); int x0 = (int16_t)rd16(port + 50), y0 = (int16_t)rd16(port + 48);
        int x1 = n == 0xA891 ? h : x0 + h, y1 = n == 0xA891 ? v : y0 + v; pe_op(0x0020); pe16(y0); pe16(x0); pe16(y1); pe16(x1); wr16(port + 48, y1); wr16(port + 50, x1); return 1; }
    case 0xA883: { uint8_t c = rd16(sp) & 0xFF; setsp(sp + 2); pic_pnloc_text(port, &c, 1); return 1; }
    case 0xA884: { uint32_t p = rd32(sp); setsp(sp + 4); uint8_t b[256]; int l = rd8(p); for (int i = 0; i < l; i++) b[i] = rd8(p + 1 + i); pic_pnloc_text(port, b, l); return 1; }
    case 0xA885: { int cnt = rd16(sp), first = rd16(sp + 2); uint32_t buf = rd32(sp + 4); setsp(sp + 8); if (cnt > 0 && cnt < 256) pic_pnloc_text(port, ram + ((buf + first) & 0xFFFFFF), cnt); return 1; }
    /* rect */
    case 0xA8A1: case 0xA8A2: case 0xA8A3: case 0xA8A4: { Rc r = rc_get(rd32(sp)); setsp(sp + 4); pe_op(0x0030 + (n - 0xA8A1)); pe_rect(r); return 1; }
    case 0xA8A5: { uint32_t pat = rd32(sp); Rc r = rc_get(rd32(sp + 4)); setsp(sp + 8); pe_op(0x000A); pe_bytes(pat, 8); pe_op(0x0034); pe_rect(r); return 1; }
    /* oval */
    case 0xA8B7: case 0xA8B8: case 0xA8B9: case 0xA8BA: { Rc r = rc_get(rd32(sp)); setsp(sp + 4); pe_op(0x0050 + (n - 0xA8B7)); pe_rect(r); return 1; }
    case 0xA8BB: { uint32_t pat = rd32(sp); Rc r = rc_get(rd32(sp + 4)); setsp(sp + 8); pe_op(0x000A); pe_bytes(pat, 8); pe_op(0x0054); pe_rect(r); return 1; }
    /* round rect */
    case 0xA8B0: case 0xA8B1: case 0xA8B2: case 0xA8B3: { int oh = (int16_t)rd16(sp), ow = (int16_t)rd16(sp + 2); Rc r = rc_get(rd32(sp + 4)); setsp(sp + 8); pe_op(0x000B); pe16(oh); pe16(ow); pe_op(0x0040 + (n - 0xA8B0)); pe_rect(r); return 1; }
    case 0xA8B4: { uint32_t pat = rd32(sp); int oh = (int16_t)rd16(sp + 4), ow = (int16_t)rd16(sp + 6); Rc r = rc_get(rd32(sp + 8)); setsp(sp + 12); pe_op(0x000A); pe_bytes(pat, 8); pe_op(0x000B); pe16(oh); pe16(ow); pe_op(0x0044); pe_rect(r); return 1; }
    /* arcs */
    case 0xA8BE: case 0xA8BF: case 0xA8C0: case 0xA8C1: { int arc = (int16_t)rd16(sp), st = (int16_t)rd16(sp + 2); Rc r = rc_get(rd32(sp + 4)); setsp(sp + 8); pe_op(0x0060 + (n - 0xA8BE)); pe_rect(r); pe16(st); pe16(arc); return 1; }
    case 0xA8C2: { uint32_t pat = rd32(sp); int arc = (int16_t)rd16(sp + 4), st = (int16_t)rd16(sp + 6); Rc r = rc_get(rd32(sp + 8)); setsp(sp + 12); pe_op(0x000A); pe_bytes(pat, 8); pe_op(0x0064); pe_rect(r); pe16(st); pe16(arc); return 1; }
    /* polygons & regions: raw handle data follows the opcode */
    case 0xA8C6: case 0xA8C7: case 0xA8C8: case 0xA8C9: { uint32_t ph = rd32(sp); setsp(sp + 4); if (ph && rd32(ph)) { pe_op(0x0070 + (n - 0xA8C6)); pe_bytes(rd32(ph), rd16(rd32(ph))); } return 1; }
    case 0xA8CA: { uint32_t pat = rd32(sp), ph = rd32(sp + 4); setsp(sp + 8); if (ph && rd32(ph)) { pe_op(0x000A); pe_bytes(pat, 8); pe_op(0x0074); pe_bytes(rd32(ph), rd16(rd32(ph))); } return 1; }
    case 0xA8D2: case 0xA8D3: case 0xA8D4: case 0xA8D5: { uint32_t rg = rd32(sp); setsp(sp + 4); if (rg && rd32(rg)) { pe_op(0x0080 + (n - 0xA8D2)); pe_bytes(rd32(rg), rd16(rd32(rg))); } return 1; }
    case 0xA8D6: { uint32_t pat = rd32(sp), rg = rd32(sp + 4); setsp(sp + 8); if (rg && rd32(rg)) { pe_op(0x000A); pe_bytes(pat, 8); pe_op(0x0084); pe_bytes(rd32(rg), rd16(rd32(rg))); } return 1; }
    case 0xA8EC: fprintf(stderr, "  [pict] CopyBits while recording is not supported\n"); return 0;
    default: return 0;
    }
}

/* ---------------- playback ---------------- */
static uint32_t pict_pixpat_tmp(void) { uint32_t h = new_handle(32); memset(ram + rd32(h), 0, 32); return h; }
static void draw_pict(uint32_t pich, Rc dest) {
    uint32_t pic = rd32(pich); if (!pic) return;
    uint32_t hs = hsize[slot_of(pich)]; Rc frame = rc_get(pic + 2); uint32_t p = pic + 10, end = pic + hs;
    int fw = frame.r - frame.l, fh = frame.b - frame.t; if (fw <= 0) fw = 1; if (fh <= 0) fh = 1;
    int dw = dest.r - dest.l, dh = dest.b - dest.t;
    uint32_t port = the_port(); if (!port) return; int v2 = 0;
    if (rd16(p) == 0x0011 && rd16(p + 2) == 0x02FF) { v2 = 1; p += 4; } else if (rd8(p) == 0x11) p += 2;
    #define MAPX(x) (dest.l + (int)(((int64_t)((x) - frame.l)) * dw / fw))
    #define MAPY(y) (dest.t + (int)(((int64_t)((y) - frame.t)) * dh / fh))
    int color = port_is_color(port);
    uint8_t saved[64]; memcpy(saved, ram + port + 32, 64); uint32_t saved_clip = rd32(port + 28), saved_pn = 0, saved_bk = 0, tmp_pn = 0, tmp_bk = 0, tmp_clip = new_rgn();
    { RL cl = {0}; rgn_get(saved_clip, &cl); rgn_set(tmp_clip, &cl); rl_free(&cl); } wr32(port + 28, tmp_clip);
    if (color) { saved_pn = rd32(port + 58); saved_bk = rd32(port + 32); tmp_pn = pict_pixpat_tmp(); tmp_bk = pict_pixpat_tmp(); uint8_t b[8]; memset(b, 0xFF, 8); wr32(port + 58, tmp_pn); ensure_pixpat(port, 58, b); memset(b, 0, 8); wr32(port + 32, tmp_bk); ensure_pixpat(port, 32, b); }
    RL base_clip = {0}; rgn_get(tmp_clip, &base_clip);
    uint8_t fillp[8]; memset(fillp, 0xFF, 8); int ovw = 0, ovh = 0, lastop_shape = -1; Rc lastrect = {0, 0, 0, 0}; (void)lastop_shape;
    int sc_pw = 1, sc_ph = 1;
    while (p + 2 <= end) {
        uint32_t op;
        if (v2) { if ((p - pic) & 1) p++; op = rd16(p); p += 2; } else { op = rd8(p); p += 1; }
        if (op == 0xFF) break;
        if (op == 0x0C00) { p += 24; continue; }
        if (op == 0x00 || op == 0x1E || op == 0x1C) continue;
        if (op == 0x01) { int sz = rd16(p); uint32_t h = new_handle(sz); memcpy(ram + rd32(h), ram + p, sz); RL rg = {0}; rgn_get(h, &rg); dispose_handle(h);
            RL m = {0}; for (int i = 0; i < rg.n; i++) { Rc r = rg.r[i]; rl_add(&m, (Rc){MAPY(r.t), MAPX(r.l), MAPY(r.b), MAPX(r.r)}); } RL sect = {0}; rl_sect(&base_clip, &m, &sect); rgn_set(tmp_clip, &sect);
            rl_free(&rg); rl_free(&m); rl_free(&sect); p += sz; continue; }
        if (op == 0x02) { uint8_t b[8]; for (int i = 0; i < 8; i++) b[i] = rd8(p + i); ensure_pixpat(port, 32, b); p += 8; continue; }
        if (op == 0x03) { wr16(port + 68, rd16(p)); p += 2; continue; }
        if (op == 0x04) { wr8(port + 70, rd8(p)); p += 1; continue; }
        if (op == 0x05) { wr16(port + 72, rd16(p)); p += 2; continue; }
        if (op == 0x06) { wr32(port + 76, rd32(p)); p += 4; continue; }
        if (op == 0x07) { int v = (int16_t)rd16(p), h = (int16_t)rd16(p + 2); p += 4; sc_pw = h < 1 ? 1 : h; sc_ph = v < 1 ? 1 : v; wr16(port + 52, (sc_ph * dh + fh / 2) / fh ? (sc_ph * dh + fh / 2) / fh : 1); wr16(port + 54, (sc_pw * dw + fw / 2) / fw ? (sc_pw * dw + fw / 2) / fw : 1); continue; }
        if (op == 0x08) { wr16(port + 56, rd16(p)); p += 2; continue; }
        if (op == 0x09) { uint8_t b[8]; for (int i = 0; i < 8; i++) b[i] = rd8(p + i); ensure_pixpat(port, 58, b); p += 8; continue; }
        if (op == 0x0A) { for (int i = 0; i < 8; i++) fillp[i] = rd8(p + i); p += 8; continue; }
        if (op == 0x0B) { ovh = (int16_t)rd16(p); ovw = (int16_t)rd16(p + 2); p += 4; continue; }
        if (op == 0x0C) { p += 4; continue; }
        if (op == 0x0D) { wr16(port + 74, rd16(p)); p += 2; continue; }
        if (op == 0x0E) { uint32_t c = rd32(p); p += 4; if (color) set_fore_rgb(port, qd_color_const(c)); wr32(port + 80, c); continue; }
        if (op == 0x0F) { uint32_t c = rd32(p); p += 4; if (color) set_back_rgb(port, qd_color_const(c)); wr32(port + 84, c); continue; }
        if (op == 0x10) { p += 8; continue; }
        if (op == 0x15 || op == 0x16 || op == 0xA0) { p += 2; continue; }
        if (op == 0x1A) { RGB c = {rd16(p), rd16(p + 2), rd16(p + 4)}; set_fore_rgb(port, c); p += 6; continue; }
        if (op == 0x1B) { RGB c = {rd16(p), rd16(p + 2), rd16(p + 4)}; set_back_rgb(port, c); p += 6; continue; }
        if (op == 0x1F || op == 0x1D) { if (color) { uint32_t gv = rd32(port + 8); if (!gv) { gv = new_handle(20); memset(ram + rd32(gv), 0, 20); wr32(port + 8, gv); } uint32_t g = rd32(gv) + (op == 0x1D ? 6 : 0); wr16(g, rd16(p)); wr16(g + 2, rd16(p + 2)); wr16(g + 4, rd16(p + 4)); } p += 6; continue; }
        if (op == 0x20 || op == 0x21 || op == 0x22 || op == 0x23) {
            int x0, y0, x1, y1;
            if (op == 0x20) { y0 = (int16_t)rd16(p); x0 = (int16_t)rd16(p + 2); y1 = (int16_t)rd16(p + 4); x1 = (int16_t)rd16(p + 6); p += 8; }
            else if (op == 0x21) { y0 = (int16_t)rd16(port + 48); x0 = (int16_t)rd16(port + 50); y1 = (int16_t)rd16(p); x1 = (int16_t)rd16(p + 2); p += 4; }
            else if (op == 0x22) { y0 = (int16_t)rd16(p); x0 = (int16_t)rd16(p + 2); y1 = y0 + (int8_t)rd8(p + 5); x1 = x0 + (int8_t)rd8(p + 4); p += 6; }
            else { y0 = (int16_t)rd16(port + 48); x0 = (int16_t)rd16(port + 50); y1 = y0 + (int8_t)rd8(p + 1); x1 = x0 + (int8_t)rd8(p); p += 2; }
            if (op == 0x21 || op == 0x23) { /* pnLoc is in dest space already */ } else { x0 = MAPX(x0); y0 = MAPY(y0); }
            x1 = (op == 0x21) ? MAPX(x1) : (op == 0x23 ? x0 + (x1 - x0) : MAPX(x1)); y1 = (op == 0x21) ? MAPY(y1) : (op == 0x23 ? y0 + (y1 - y0) : MAPY(y1));
            DC d; dc_init(&d, port); int pw = (int16_t)rd16(port + 54), ph = (int16_t)rd16(port + 52); if (!pen_hidden(port)) pen_line(&d, x0, y0, x1, y1, pw, ph); dc_free(&d); wr16(port + 48, y1); wr16(port + 50, x1); continue; }
        if (op >= 0x28 && op <= 0x2B) {
            int x = (int16_t)rd16(port + 50), y = (int16_t)rd16(port + 48);
            if (op == 0x28) { y = MAPY((int16_t)rd16(p)); x = MAPX((int16_t)rd16(p + 2)); p += 4; } else if (op == 0x29) { x += rd8(p); p += 1; } else if (op == 0x2A) { y += rd8(p); p += 1; } else { x += rd8(p); y += rd8(p + 1); p += 2; }
            int cnt = rd8(p); p++; DC d; dc_init(&d, port); wr16(port + 48, y); wr16(port + 50, x); draw_text(&d, ram + p, cnt); dc_free(&d); p += cnt; continue; }
        if (op >= 0x30 && op <= 0x7C && ((op & 7) <= 4) && ((op >> 3) >= 6 && (op >> 3) <= 0xF)) {
            /* shapes: 0x30 rect, 0x38 same-rect, 0x40 rrect, 0x48 same, 0x50 oval, 0x58, 0x60 arc, 0x68, 0x70 poly, 0x78 */
            int grp = op >> 3, which = op & 7; int kind = -1; int same = (grp & 1); int base = grp & ~1;
            Rc r = lastrect; int st = 0, ar = 0;
            if (base == 6 || base == 8 || base == 10 || base == 12) {
                if (!same) { r = rc_get(p); p += 8; lastrect = r; }
                if (base == 12) { st = (int16_t)rd16(p); ar = (int16_t)rd16(p + 2); p += 4; }
                kind = base == 6 ? SH_RECT : base == 8 ? SH_RRECT : base == 10 ? SH_OVAL : -2;
                Rc mr = {MAPY(r.t), MAPX(r.l), MAPY(r.b), MAPX(r.r)}; DC d; dc_init(&d, port);
                int pw = (int16_t)rd16(port + 54), ph = (int16_t)rd16(port + 52); if (pw < 1) pw = 1; if (ph < 1) ph = 1;
                if (kind == -2) {
                    if (which == 0) { if (!pen_hidden(port)) arc_paint(&d, mr, st, ar, 1, pw, ph); } else if (which == 1) { if (!pen_hidden(port)) arc_paint(&d, mr, st, ar, 0, 1, 1); }
                    else if (which == 2) { dc_bk_pat(&d); d.mode = 8; arc_paint(&d, mr, st, ar, 0, 1, 1); }
                    else if (which == 3) { uint8_t b[8]; memset(b, 0xFF, 8); d.fg = depth_mask(d.s.depth); dc_set_pat8(&d, b); d.mode = 10; arc_paint(&d, mr, st, ar, 0, 1, 1); }
                    else { dc_set_pat8(&d, fillp); d.mode = 8; arc_paint(&d, mr, st, ar, 0, 1, 1); }
                } else if (which == 4) { dc_set_pat8(&d, fillp); d.mode = 8; shape_paint(&d, kind, mr, ovw, ovh); }
                else shape_op(&d, which, kind, mr, ovw, ovh);
                dc_free(&d); continue; }
            /* poly / region shapes carry raw handle data */
            if (base == 14) { int sz = rd16(p); if (!same) { p += sz; } (void)sz; continue; }
            continue; }
        if (op >= 0x80 && op <= 0x84) { int sz = rd16(p); uint32_t h = new_handle(sz); memcpy(ram + rd32(h), ram + p, sz); RL rg = {0}; rgn_get(h, &rg); dispose_handle(h);
            RL m = {0}; for (int i = 0; i < rg.n; i++) { Rc r = rg.r[i]; rl_add(&m, (Rc){MAPY(r.t), MAPX(r.l), MAPY(r.b), MAPX(r.r)}); } uint32_t mh = new_rgn(); rgn_set(mh, &m);
            DC d; dc_init(&d, port); int which = op - 0x80;
            if (which == 0) { int pw = (int16_t)rd16(port + 54), ph = (int16_t)rd16(port + 52); if (!pen_hidden(port)) rgn_frame(&d, mh, pw < 1 ? 1 : pw, ph < 1 ? 1 : ph); } else if (which == 1) { if (!pen_hidden(port)) rgn_paint(&d, mh); }
            else if (which == 2) { dc_bk_pat(&d); d.mode = 8; rgn_paint(&d, mh); } else if (which == 3) { uint8_t b[8]; memset(b, 0xFF, 8); d.fg = depth_mask(d.s.depth); dc_set_pat8(&d, b); d.mode = 10; rgn_paint(&d, mh); } else { dc_set_pat8(&d, fillp); d.mode = 8; rgn_paint(&d, mh); }
            dc_free(&d); dispose_handle(mh); rl_free(&rg); rl_free(&m); p += sz; continue; }
        if (op == 0x98 || op == 0x99 || op == 0x90 || op == 0x91) {
            DC d; dc_init(&d, port);
            uint32_t rbw = rd16(p); Surf s; uint32_t pm = p; int packed = (op == 0x98 || op == 0x99);
            int ispix = (rbw & 0x8000) != 0; uint32_t rowb = rbw & 0x3FFF; Rc bnd = rc_get(p + 2); int depth = 1; uint32_t ctab = 0; uint32_t q = p;
            int pack = 0; Rc srcr, dstr; uint32_t maskh = 0;
            if (ispix) {
                pack = (int16_t)rd16(pm + 12); depth = rd16(pm + 28); q = pm + 46;
                uint32_t ctsize = rd16(q + 6) + 1; ctab = new_handle(8 + 8 * ctsize); uint32_t cp = rd32(ctab); wr32(cp, ++ctseed); wr16(cp + 6, ctsize - 1);
                for (uint32_t i = 0; i < ctsize; i++) { for (int k = 0; k < 8; k += 2) wr16(cp + 8 + i * 8 + k, rd16(q + 8 + i * 8 + k)); wr16(cp + 8 + i * 8, i); }
                q += 8 + ctsize * 8;
            } else q = p + 10;
            srcr = rc_get(q); dstr = rc_get(q + 8); int pmode = rd16(q + 16); q += 18;
            if (op == 0x99 || op == 0x91) { uint32_t rs = rd16(q); maskh = new_handle(rs); memcpy(ram + rd32(maskh), ram + q, rs); q += rs; }
            int h = bnd.b - bnd.t, w = bnd.r - bnd.l; uint32_t outrb = ((w * depth + 31) / 32) * 4; uint32_t buf = alloc_block(outrb * (h ? h : 1));
            if (!buf) { if (ctab) dispose_handle(ctab); dc_free(&d); break; }
            for (int y = 0; y < h; y++) {
                if (!packed || (pack == 1) || (rowb < 8)) { memcpy(ram + buf + y * outrb, ram + q, rowb < outrb ? rowb : outrb); q += rowb; }
                else {
                    uint32_t nb; if (rowb > 250) { nb = rd16(q); q += 2; } else { nb = rd8(q); q += 1; }
                    if (depth == 16 && pack == 3) { uint8_t tmp[4096]; unpack_bits(ram + q, nb, tmp, w * 2); memcpy(ram + buf + y * outrb, tmp, w * 2 < (int)outrb ? w * 2 : outrb); }
                    else unpack_bits(ram + q, nb, ram + buf + y * outrb, outrb);
                    q += nb;
                }
            }
            s.base = buf; s.rb = outrb; s.depth = depth; s.bnd = bnd; s.ctab = ctab; s.color = ispix;
            Rc ddr = {MAPY(dstr.t), MAPX(dstr.l), MAPY(dstr.b), MAPX(dstr.r)};
            copybits(&s, &d, srcr, ddr, pmode & 0x3F, maskh);
            free_block(buf); if (ctab) dispose_handle(ctab); if (maskh) dispose_handle(maskh); dc_free(&d);
            p = q; continue;
        }
        if (op == 0x9A || op == 0x9B) { fprintf(stderr, "  [pict] DirectBits not supported\n"); break; }
        if (op == 0xA1) { p += 2; p += 2 + rd16(p); continue; }
        if (op >= 0x100 && op < 0x8000) { p += (op >> 8) * 2; continue; }
        fprintf(stderr, "  [pict] unhandled op %04x at %06x\n", op, p); break;
    }
    rl_free(&base_clip);
    memcpy(ram + port + 32, saved, 64); wr32(port + 28, saved_clip); dispose_handle(tmp_clip);
    if (color) { wr32(port + 58, saved_pn); wr32(port + 32, saved_bk); if (tmp_pn) dispose_handle(tmp_pn); if (tmp_bk) dispose_handle(tmp_bk); }
    gfx_dirty = 1;
}
