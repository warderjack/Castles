/* ---- Control Manager (push buttons, check boxes, radio buttons) + Dialog Manager + Alerts ---- */
/* Blocking/guest-calling traps are state machines: a handler that must run 68k code (filter or user-item procs)
   sets up a guest call whose return address is GCONT_PC; when the proc returns, the original trap re-executes. */

static struct { uint32_t sp_rew, pc_rew; uint32_t res16, res32; } gc;
static void guest_begin(uint32_t ret) { if (trap_from_stub) push32(ret); gc.sp_rew = SP; gc.pc_rew = trap_pc; }
static void guest_push16(uint32_t v) { setsp(SP - 2); wr16(SP, v); }
static void guest_push32(uint32_t v) { push32(v); }
static void guest_go(uint32_t proc) { push32(GCONT_PC); W(M68K_REG_PC, proc); wm_redirected = 1; }
static void gc_resume(void) { gc.res16 = rd16(SP); gc.res32 = rd32(SP); setsp(gc.sp_rew); W(M68K_REG_PC, gc.pc_rew); }

/* ---------------- Controls ---------------- */
/* ControlRecord: next +0, owner +4, rect +8, vis +16, hilite +17, value +18, min +20, max +22, defproc +24, data +28, action +32, refcon +36, title +40 */
static uint32_t ctl_new(uint32_t owner, Rc r, const uint8_t *title, int tl, int vis, int val, int mn, int mx, int proc, uint32_t ref) {
    uint32_t ch = new_handle(40 + 256); if (!ch) return 0; uint32_t c = rd32(ch); memset(ram + c, 0, 296);
    wr32(c + 4, owner); rc_put(c + 8, r); wr8(c + 16, vis ? 255 : 0); wr8(c + 17, 0); wr16(c + 18, val); wr16(c + 20, mn); wr16(c + 22, mx); wr16(c + 24, proc);
    wr32(c + 36, ref); wr8(c + 40, tl); for (int i = 0; i < tl; i++) wr8(c + 41 + i, title[i]);
    if (owner) { wr32(c, rd32(owner + 140)); wr32(owner + 140, ch); }
    return ch;
}
static void port_dc(uint32_t port, DC *d) { dc_init(d, port); }
static void set_black(DC *d) { uint8_t b[8]; memset(b, 0xFF, 8); dc_set_pat8(d, b); d->mode = 8; }
static void set_white(DC *d) { uint8_t b[8]; memset(b, 0, 8); dc_set_pat8(d, b); d->mode = 8; }
static const FStrike *port_strike(uint32_t port) { return pick_strike((int16_t)rd16(port + 68), rd8(port + 70), (int16_t)rd16(port + 74)); }
static void port_text(DC *d, int x, int y, const char *s, int n) {
    uint32_t port = d->port, pl = rd32(port + 48); int sm = rd16(port + 72); wr16(port + 48, y); wr16(port + 50, x); wr16(port + 72, 1);
    draw_text(d, (const uint8_t *)s, n); wr16(port + 72, sm); wr32(port + 48, pl);
}
static void gray_rect(DC *d, Rc r) {
    for (int y = r.t; y < r.b; y++) for (int x = r.l; x < r.r; x++) if (((x + y) & 1) == 0) {
        int in = 0; for (int i = 0; i < d->clip.n; i++) { Rc c = d->clip.r[i]; if (x >= c.l && x < c.r && y >= c.t && y < c.b) { in = 1; break; } }
        if (in && gp(&d->s, x, y) == d->fg) pp(&d->s, x, y, d->bg); }
}
static void ctl_draw(uint32_t ch) {
    uint32_t c = ch ? rd32(ch) : 0; if (!c || !rd8(c + 16)) return;
    uint32_t port = rd32(c + 4); if (!port) return;
    Rc r = rc_get(c + 8); int hil = rd8(c + 17), val = (int16_t)rd16(c + 18), proc = rd16(c + 24) & 0xF; char t[256]; int tl = rd8(c + 40); for (int i = 0; i < tl; i++) t[i] = rd8(c + 41 + i);
    DC d; port_dc(port, &d); const FStrike *f = port_strike(port); int asc = f ? f->ascent : 10, dsc = f ? f->descent : 3;
    int base = r.t + ((r.b - r.t) + asc - dsc) / 2;
    set_white(&d); dc_rect(&d, r);
    if (proc == 0) {
        set_black(&d); shape_frame(&d, SH_RRECT, r, 16, 16, 1, 1);
        int w = str_width(port, (const uint8_t *)t, tl); port_text(&d, (r.l + r.r - w) / 2, base, t, tl);
        if (hil == 1) shape_invert(&d, SH_RRECT, r, 16, 16);
        if (hil == 255) gray_rect(&d, r);
    } else {
        Rc box = {(r.t + r.b) / 2 - 6, r.l, (r.t + r.b) / 2 + 6, r.l + 12}; set_black(&d);
        if (proc == 1) { frame_rect_dc(&d, box, 1); if (val) { pen_line(&d, box.l, box.t, box.r - 1, box.b - 1, 1, 1); pen_line(&d, box.l, box.b - 1, box.r - 1, box.t, 1, 1); } }
        else { shape_frame(&d, SH_OVAL, box, 0, 0, 1, 1); if (val) shape_paint(&d, SH_OVAL, (Rc){box.t + 3, box.l + 3, box.b - 3, box.r - 3}, 0, 0); }
        port_text(&d, r.l + 18, base, t, tl);
        if (hil == 1) { Rc ib = {box.t + 1, box.l + 1, box.b - 1, box.r - 1}; invert_rect_dc(&d, ib); }
        if (hil == 255) gray_rect(&d, r);
    }
    dc_free(&d);
}
static void ctl_dispose(uint32_t ch) {
    if (!ch || !rd32(ch)) return; uint32_t c = rd32(ch), owner = rd32(c + 4);
    if (owner) { uint32_t *pp_ = NULL; (void)pp_; uint32_t prev = 0, p = rd32(owner + 140); while (p && p != ch) { prev = p; p = rd32(rd32(p)); }
        if (p) { if (prev) wr32(rd32(prev), rd32(rd32(p))); else wr32(owner + 140, rd32(rd32(p))); } }
    dispose_handle(ch);
}

/* ---------------- Dialogs ---------------- */
typedef struct { Rc box; int type; int enabled; uint32_t h; } DItem;
typedef struct { uint32_t rec; int n; DItem it[48]; int edit; int selall; int used; } Dlg;
static Dlg dlgs[16];
static char ptxt[4][256];
static Dlg *dlg_of(uint32_t rec) { for (int i = 0; i < 16; i++) if (dlgs[i].used && dlgs[i].rec == rec) return &dlgs[i]; return NULL; }
static uint32_t md_scratch = 0;
static uint32_t htext_len(uint32_t h) { return h ? hsize[slot_of(h)] : 0; }
static void htext_get(uint32_t h, char *out, int max) { int l = htext_len(h); if (l >= max) l = max - 1; for (int i = 0; i < l; i++) out[i] = rd8(rd32(h) + i); out[l] = 0; }
static void htext_set(uint32_t h, const char *s, int n) { set_handle_size(h, n ? n : 1); hsize[slot_of(h)] = n; for (int i = 0; i < n; i++) wr8(rd32(h) + i, s[i]); }
static void subst(const char *in, int n, char *out) {
    int o = 0; for (int i = 0; i < n && o < 1000; i++) {
        if (in[i] == '^' && i + 1 < n && in[i + 1] >= '0' && in[i + 1] <= '3') { const char *p = ptxt[in[i + 1] - '0']; while (*p && o < 1000) out[o++] = *p++; i++; }
        else out[o++] = in[i]; }
    out[o] = 0;
}
static Rc item_inflate(Rc r, int n) { return (Rc){r.t - n, r.l - n, r.b + n, r.r + n}; }
static void dlg_items_build(Dlg *d, const uint8_t *b, int len) {
    int n = (int)(int16_t)be16(b) + 1; if (n > 48) n = 48; int o = 2; d->n = 0; d->edit = -1; d->selall = 0;
    for (int i = 0; i < n && o + 14 <= len; i++) {
        DItem *it = &d->it[d->n]; memset(it, 0, sizeof *it);
        it->box = (Rc){(int16_t)be16(b + o + 4), (int16_t)be16(b + o + 6), (int16_t)be16(b + o + 8), (int16_t)be16(b + o + 10)};
        int ty = b[o + 12]; int dl = b[o + 13]; const uint8_t *dp = b + o + 14; o += 14 + dl + (dl & 1);
        it->type = ty & 0x7F; it->enabled = !(ty & 0x80);
        switch (it->type) {
        case 4: case 5: case 6: it->h = ctl_new(d->rec, it->box, dp, dl, 1, 0, 0, 1, it->type - 4, 0); break;
        case 7: { it->h = ctl_new(d->rec, it->box, (const uint8_t *)"", 0, 1, 0, 0, 1, 0, 0); break; }
        case 8: case 16: it->h = new_handle(dl ? dl : 1); hsize[slot_of(it->h)] = dl; for (int k = 0; k < dl; k++) wr8(rd32(it->h) + k, dp[k]); if (it->type == 16 && d->edit < 0) { d->edit = d->n; d->selall = 1; } break;
        case 32: { Res *r = find_res(0x49434F4E, (int16_t)be16(dp)); it->h = r ? res_get_handle(r) : 0; break; }
        case 64: { Res *r = find_res(0x50494354, (int16_t)be16(dp)); it->h = r ? res_get_handle(r) : 0; break; }
        default: break;
        }
        d->n++;
    }
}
static uint32_t dlg_create(uint32_t storage, Rc bounds, uint32_t title, int visible, int proc, uint32_t behind, int goaway, uint32_t refcon, const uint8_t *ditl, int dlen) {
    Dlg *d = NULL; for (int i = 0; i < 16; i++) if (!dlgs[i].used) { d = &dlgs[i]; break; } if (!d) return 0;
    if (!storage) storage = alloc_block(200); if (!storage) return 0; memset(ram + storage, 0, 200);
    uint32_t w = wm_make_window(storage, bounds, title, 0, proc, behind, goaway, refcon, 0); if (!w) return 0;
    wr16(w + 108, 2); memset(d, 0, sizeof *d); d->used = 1; d->rec = w; dlg_items_build(d, ditl, dlen);
    uint32_t ih = new_handle(dlen ? dlen : 1); for (int i = 0; i < dlen; i++) wr8(rd32(ih) + i, ditl[i]);
    wr32(w + 156, ih); wr32(w + 160, 0); wr16(w + 164, d->edit < 0 ? 0xFFFF : d->edit); wr16(w + 166, 0); wr16(w + 168, 1);
    if (visible) wm_show_hide(w, 1);
    return w;
}
static void dlg_dispose(uint32_t w) {
    Dlg *d = dlg_of(w); if (d) { for (int i = 0; i < d->n; i++) { DItem *it = &d->it[i]; if (it->type >= 4 && it->type <= 7) ctl_dispose(it->h); else if (it->type == 8 || it->type == 16) dispose_handle(it->h); } d->used = 0; }
    if (rd32(w + 156)) dispose_handle(rd32(w + 156));
    wm_dispose(w, 0); free_block(w);
}
static int wrap_text(uint32_t port, const char *s, int n, int width, int *starts, int *lens, int maxl) {
    int nl = 0, i = 0;
    while (i < n && nl < maxl) {
        int ls = i, w = 0, lastsp = -1, j = i;
        while (j < n && s[j] != '\r') { int cw = str_width(port, (const uint8_t *)&s[j], 1); if (w + cw > width && j > ls) break; if (s[j] == ' ') lastsp = j; w += cw; j++; }
        if (j < n && s[j] != '\r' && lastsp >= 0) { starts[nl] = ls; lens[nl] = lastsp - ls; nl++; i = lastsp + 1; continue; }
        starts[nl] = ls; lens[nl] = j - ls; nl++; i = (j < n && s[j] == '\r') ? j + 1 : j;
    }
    return nl;
}
static void draw_item(Dlg *d, int i) {
    DItem *it = &d->it[i]; uint32_t port = d->rec; DC dc; port_dc(port, &dc); const FStrike *f = port_strike(port); int asc = f ? f->ascent : 10, dsc = f ? f->descent : 3; int lh = asc + dsc + 1;
    Rc b = it->box;
    switch (it->type) {
    case 4: case 5: case 6: case 7: dc_free(&dc); ctl_draw(it->h);
        if (it->type == 4 && (int)i + 1 == (int16_t)rd16(port + 168) && (int16_t)rd16(port + 168) > 0) { port_dc(port, &dc); set_black(&dc); shape_frame(&dc, SH_RRECT, item_inflate(b, 4), 16, 16, 3, 3); dc_free(&dc); }
        return;
    case 8: { char raw[1024], t[1100]; htext_get(it->h, raw, sizeof raw); subst(raw, strlen(raw), t); int st[64], ln[64]; int nl = wrap_text(port, t, strlen(t), b.r - b.l, st, ln, 64);
        RL save = dc.clip; RL bx = {0}, ns = {0}; rl_add(&bx, b); rl_sect(&save, &bx, &ns); dc.clip = ns; set_black(&dc);
        for (int k = 0; k < nl; k++) port_text(&dc, b.l, b.t + asc + k * lh, t + st[k], ln[k]);
        rl_free(&bx); rl_free(&save); break; }
    case 16: { char t[512]; htext_get(it->h, t, sizeof t); Rc fr = item_inflate(b, 3); set_white(&dc); dc_rect(&dc, fr); set_black(&dc); frame_rect_dc(&dc, fr, 1);
        RL save = dc.clip; RL bx = {0}, ns = {0}; rl_add(&bx, b); rl_sect(&save, &bx, &ns); dc.clip = ns;
        int tl = strlen(t), w = str_width(port, (const uint8_t *)t, tl);
        if (d->edit == i && d->selall && tl) { set_black(&dc); dc_rect(&dc, (Rc){b.t, b.l, b.t + lh, b.l + w}); uint32_t fg = dc.fg; dc.fg = dc.bg; dc.bg = fg; port_text(&dc, b.l, b.t + asc, t, tl); dc.bg = dc.fg; dc.fg = fg; }
        else { port_text(&dc, b.l, b.t + asc, t, tl); if (d->edit == i) { set_black(&dc); dc_rect(&dc, (Rc){b.t, b.l + w, b.t + lh, b.l + w + 1}); } }
        rl_free(&bx); rl_free(&save); break; }
    case 32: { if (!it->h) break; uint32_t p = rd32(it->h); set_black(&dc); for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) if (rd8(p + y * 4 + (x >> 3)) & (0x80 >> (x & 7))) { dc_span(&dc, b.t + y, b.l + x, b.l + x + 1); } break; }
    case 64: dc_free(&dc); if (it->h) draw_pict(it->h, b); return;
    default: break;
    }
    dc_free(&dc);
}

static struct { int active; uint32_t dlg; int idx; int erase; } ddr;
static int dlg_draw_step(uint32_t ret) {       /* 1 = guest call launched (caller returns), 0 = finished */
    Dlg *d = dlg_of(ddr.dlg); if (!d) { ddr.active = 0; return 0; }
    set_the_port(ddr.dlg);
    if (ddr.erase) { ddr.erase = 0; DC dc; port_dc(ddr.dlg, &dc); set_white(&dc); dc_rect(&dc, rc_get(ddr.dlg + 16)); dc_free(&dc); }
    while (ddr.idx < d->n) {
        int i = ddr.idx++; DItem *it = &d->it[i];
        if (it->type == 0 && it->h) { guest_begin(ret); guest_push32(ddr.dlg); guest_push16(i + 1); guest_go(it->h); return 1; }
        draw_item(d, i);
    }
    ddr.active = 0; return 0;
}
static void dlg_begin_draw(uint32_t dlg, int erase) { ddr.active = 1; ddr.dlg = dlg; ddr.idx = 0; ddr.erase = erase; }
static void win_validate_all(uint32_t w) { rgn_set_rect(rd32(w + WR_UPDATE), (Rc){0, 0, 0, 0}); }
static int dlg_find_item(Dlg *d, int lx, int ly) { for (int i = 0; i < d->n; i++) { Rc b = d->it[i].box; if (lx >= b.l && lx < b.r && ly >= b.t && ly < b.b) return i; } return -1; }
static void edit_redraw(Dlg *d) { if (d->edit >= 0) draw_item(d, d->edit); }
static void edit_key(Dlg *d, int ch) {
    if (d->edit < 0) return; DItem *it = &d->it[d->edit]; char t[512]; htext_get(it->h, t, sizeof t); int tl = strlen(t);
    if (ch == 8) { if (d->selall) tl = 0; else if (tl) tl--; d->selall = 0; }
    else if (ch >= 32 && ch != 127 && tl < 250) { if (d->selall) tl = 0; t[tl++] = ch; d->selall = 0; }
    else return;
    htext_set(it->h, t, tl); set_the_port(d->rec); edit_redraw(d);
}

/* ---- modal loop (ModalDialog / Alert) ---- */
static struct { int active; uint32_t dlg; int phase; int track; int inside; uint32_t until; int have_ev; int er; int filt_state; int hit; } md;
static void md_hilite(Dlg *d, int item, int on) { DItem *it = &d->it[item]; if (it->type >= 4 && it->type <= 6) { wr8(rd32(it->h) + 17, on ? 1 : 0); set_the_port(d->rec); ctl_draw(it->h); } }
/* returns 1 when an item was hit (md.hit set), 0 when still waiting (trap rewound or guest call launched) */
static int modal_step(uint32_t ret, uint32_t filter) {
    Dlg *d = dlg_of(md.dlg); if (!d) return 1;
    if (!md_scratch) md_scratch = alloc_block(48);
    uint32_t evp = md_scratch, itp = md_scratch + 20;
    if (ddr.active) { if (dlg_draw_step(ret)) return 0; }
    if (md.phase == 1) {         /* tracking a control */
        ev_pump(); Dlg *dd = d; int item = md.track; DItem *it = &dd->it[item]; int ox, oy; win_origin(md.dlg, &ox, &oy); int lx = plat_mx - ox, ly = plat_my - oy;
        Rc b = it->box; int inside = lx >= b.l && lx < b.r && ly >= b.t && ly < b.b;
        if (plat_btn) { if (inside != md.inside) { md.inside = inside; md_hilite(dd, item, inside); } idle_hint = 1; rewind_trap(ret); return 0; }
        md_hilite(dd, item, 0); md.phase = 0; if (md.inside) { md.hit = item + 1; return 1; } md.have_ev = 0; }
    if (md.phase == 2) {         /* key flash */
        ev_pump(); if (ticks_now() < md.until) { idle_hint = 1; rewind_trap(ret); return 0; }
        md_hilite(d, md.track, 0); md.phase = 0; md.hit = md.track + 1; return 1; }
    if (!md.have_ev) { md.er = get_event(evp, 0xFFFF, 1); md.have_ev = 1; md.filt_state = 0; }
    int what = rd16(evp);
    if (filter && md.filt_state == 0) {
        md.filt_state = 1; wr16(itp, 0); guest_begin(ret); guest_push16(0); guest_push32(md.dlg); guest_push32(evp); guest_push32(itp); guest_go(filter); return 0; }
    if (filter && md.filt_state == 1) { md.filt_state = 2; if (gc.res16 & 0xFF00) { md.have_ev = 0; md.hit = (int16_t)rd16(itp); return 1; } }
    md.have_ev = 0;
    if (what == 6) { uint32_t w = rd32(evp + 2); if (w == md.dlg) { win_validate_all(w); dlg_begin_draw(w, 1); } else if (is_window(w)) win_validate_all(w); rewind_trap(ret); return 0; }
    if (what == 1) {
        int ox, oy; win_origin(md.dlg, &ox, &oy); int gv = (int16_t)rd16(evp + 10), gh = (int16_t)rd16(evp + 12); int lx = gh - ox, ly = gv - oy; int i = dlg_find_item(d, lx, ly);
        if (i >= 0 && d->it[i].enabled) { DItem *it = &d->it[i];
            if (it->type >= 4 && it->type <= 6) { if (rd8(rd32(it->h) + 17) != 255) { md.track = i; md.inside = 1; md.phase = 1; md_hilite(d, i, 1); } }
            else if (it->type == 16) { if (d->edit != i) { int old = d->edit; d->edit = i; d->selall = 1; set_the_port(md.dlg); if (old >= 0) draw_item(d, old); edit_redraw(d); } }
            else if (it->type == 0 || it->type == 7) { md.hit = i + 1; return 1; } }
        rewind_trap(ret); return 0; }
    if (what == 3 || what == 5) {
        int ch = rd32(evp + 2) & 0xFF;
        if (ch == 13 || ch == 3) { int def = (int16_t)rd16(md.dlg + 168) - 1; if (def >= 0 && def < d->n) { DItem *it = &d->it[def]; if (it->type >= 4 && it->type <= 6) { md.track = def; md_hilite(d, def, 1); md.until = ticks_now() + 8; md.phase = 2; } else { md.hit = def + 1; return 1; } } }
        else if (ch == 9) { int n = d->n, c = d->edit; for (int k = 1; k <= n && c >= 0; k++) { int j = (c + k) % n; if (d->it[j].type == 16) { set_the_port(md.dlg); int old = d->edit; d->edit = j; d->selall = 1; draw_item(d, old); edit_redraw(d); break; } } }
        else edit_key(d, ch);
        rewind_trap(ret); return 0; }
    if (!md.er) idle_hint = 1;
    rewind_trap(ret); return 0;
}
static void md_start(uint32_t dlg) { memset(&md, 0, sizeof md); md.active = 1; md.dlg = dlg; set_the_port(dlg); win_validate_all(dlg); }


/* ---- PopUpMenuSelect ---- */
static struct { int active; uint32_t mh; Rc box, sa; uint8_t *saved; int item; } pu;
static void pu_draw_item(DC *d, int i, int hil) { dd_item_draw(d, pu.mh, i, pu.box, hil); }
static int pu_step(uint32_t ret, uint32_t mh, int top, int left, int popitem, uint32_t *result) {
    if (!pu.active) {
        memset(&pu, 0, sizeof pu); pu.active = 1; pu.mh = mh; int w, h; menu_calc(mh, &w, &h); int n = menu_count(mh); if (popitem < 1 || popitem > n) popitem = 1;
        int bt = top - (popitem - 1) * 16, bl = left; if (bt < mbar_height) bt = mbar_height; if (bt + h + 2 > SCR_H) bt = SCR_H - h - 2; if (bl + w + 2 > SCR_W) bl = SCR_W - w - 2; if (bl < 0) bl = 0;
        pu.box = (Rc){bt, bl, bt + h + 1, bl + w + 1}; pu.sa = (Rc){pu.box.t, pu.box.l, pu.box.b + 1, pu.box.r + 1};
        int sw = pu.sa.r - pu.sa.l, sh = pu.sa.b - pu.sa.t; pu.saved = malloc(sw * sh);
        for (int y = 0; y < sh; y++) memcpy(pu.saved + y * sw, ram + FB_BASE + (pu.sa.t + y) * SCR_W + pu.sa.l, sw);
        RL all = {0}; rl_add(&all, pu.sa); DC d; screen_dc(&d, &all);
        fill_rect_dc(&d, (Rc){pu.box.b, pu.box.l + 1, pu.box.b + 1, pu.box.r + 1}, 1); fill_rect_dc(&d, (Rc){pu.box.t + 1, pu.box.r, pu.box.b + 1, pu.box.r + 1}, 1);
        fill_rect_dc(&d, pu.box, 0); frame_rect_dc(&d, pu.box, 1);
        for (int i = 1; i <= n; i++) pu_draw_item(&d, i, 0);
        pu.item = 0; dc_free(&d); rl_free(&all);
        if (menu_item_enabled(mh, popitem)) { pu.item = popitem; RL a2 = {0}; rl_add(&a2, pu.sa); DC d2; screen_dc(&d2, &a2); pu_draw_item(&d2, popitem, 1); dc_free(&d2); rl_free(&a2); }
    }
    ev_pump(); int mx = plat_mx, my = plat_my, n = menu_count(pu.mh); int it = 0;
    if (mx >= pu.box.l && mx < pu.box.r - 1 && my >= pu.box.t && my < pu.box.b - 1) { it = (my - pu.box.t) / 16 + 1; if (it > n || !menu_item_enabled(pu.mh, it)) it = 0; }
    if (it != pu.item) { RL a2 = {0}; rl_add(&a2, pu.sa); DC d2; screen_dc(&d2, &a2); if (pu.item) pu_draw_item(&d2, pu.item, 0); if (it) pu_draw_item(&d2, it, 1); dc_free(&d2); rl_free(&a2); pu.item = it; }
    if (plat_btn) { idle_hint = 1; rewind_trap(ret); return 0; }
    int sw = pu.sa.r - pu.sa.l, sh = pu.sa.b - pu.sa.t; for (int y = 0; y < sh; y++) memcpy(ram + FB_BASE + (pu.sa.t + y) * SCR_W + pu.sa.l, pu.saved + y * sw, sw);
    free(pu.saved); pu.saved = NULL; gfx_dirty = 1; *result = pu.item ? (((uint32_t)(menu_id(pu.mh) & 0xFFFF)) << 16) | (uint32_t)pu.item : 0; pu.active = 0; return 1;
}
static struct { int active; uint32_t dlg; } al;
static int dlg_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    uint32_t sp = SP;
    switch (n) {
    /* ---- Control Manager ---- */
    case 0xA954: { uint32_t ref = rd32(sp); int proc = (int16_t)rd16(sp + 4), mx = (int16_t)rd16(sp + 6), mn = (int16_t)rd16(sp + 8), val = (int16_t)rd16(sp + 10); int vis = rd8(sp + 12) != 0; uint32_t title = rd32(sp + 14), rect = rd32(sp + 18), owner = rd32(sp + 22);
        uint8_t tb[256]; int tl = title ? rd8(title) : 0; for (int i = 0; i < tl; i++) tb[i] = rd8(title + 1 + i);
        setsp(sp + 26); uint32_t ch = ctl_new(owner, rc_get(rect), tb, tl, vis, val, mn, mx, proc, ref); wr32(sp + 26, ch); if (vis) ctl_draw(ch); return 1; }
    case 0xA955: { uint32_t ch = rd32(sp); setsp(sp + 4); ctl_dispose(ch); return 1; }
    case 0xA956: { uint32_t wn = rd32(sp); setsp(sp + 4); while (rd32(wn + 140)) ctl_dispose(rd32(wn + 140)); return 1; }
    case 0xA957: { uint32_t ch = rd32(sp); setsp(sp + 4); wr8(rd32(ch) + 16, 255); ctl_draw(ch); return 1; }
    case 0xA958: { uint32_t ch = rd32(sp); setsp(sp + 4); uint32_t c = rd32(ch); wr8(c + 16, 0); DC dc; port_dc(rd32(c + 4), &dc); set_white(&dc); dc_rect(&dc, rc_get(c + 8)); dc_free(&dc); return 1; }
    case 0xA959: { uint32_t ch = rd32(sp + 4); int hh = (int16_t)rd16(sp + 2), vv = (int16_t)rd16(sp); setsp(sp + 8);
        uint32_t c = rd32(ch); Rc r = rc_get(c + 8); int w_ = r.r - r.l, h_ = r.b - r.t; rc_put(c + 8, (Rc){vv, hh, vv + h_, hh + w_}); return 1; }
    case 0xA95C: { uint32_t ch = rd32(sp + 4); int ww = (int16_t)rd16(sp + 2), hh = (int16_t)rd16(sp); setsp(sp + 8); uint32_t c = rd32(ch); Rc r = rc_get(c + 8); rc_put(c + 8, (Rc){r.t, r.l, r.t + hh, r.l + ww}); return 1; }
    case 0xA95A: { uint32_t ch = rd32(sp); setsp(sp + 4); wr32(sp + 4, rd32(rd32(ch) + 36)); return 1; }
    case 0xA95B: { uint32_t ch = rd32(sp + 4); uint32_t v = rd32(sp); setsp(sp + 8); wr32(rd32(ch) + 36, v); return 1; }
    case 0xA95D: { uint32_t ch = rd32(sp + 2); int st = rd16(sp); setsp(sp + 6); uint32_t c = rd32(ch); if (rd8(c + 17) != st) { wr8(c + 17, st); ctl_draw(ch); } return 1; }
    case 0xA95E: { uint32_t ch = rd32(sp + 4), out = rd32(sp); setsp(sp + 8); uint32_t c = rd32(ch); int l = rd8(c + 40); wr8(out, l); for (int i = 0; i < l; i++) wr8(out + 1 + i, rd8(c + 41 + i)); return 1; }
    case 0xA95F: { uint32_t ch = rd32(sp + 4), t = rd32(sp); setsp(sp + 8); uint32_t c = rd32(ch); int l = rd8(t); wr8(c + 40, l); for (int i = 0; i < l; i++) wr8(c + 41 + i, rd8(t + 1 + i)); ctl_draw(ch); return 1; }
    case 0xA960: case 0xA961: case 0xA962: { uint32_t ch = rd32(sp); setsp(sp + 4); uint32_t c = rd32(ch); wr16(sp + 4, rd16(c + (n == 0xA960 ? 18 : n == 0xA961 ? 20 : 22))); return 1; }
    case 0xA963: case 0xA964: case 0xA965: { uint32_t ch = rd32(sp + 2); int v = (int16_t)rd16(sp); setsp(sp + 6); uint32_t c = rd32(ch); uint32_t a = c + (n == 0xA963 ? 18 : n == 0xA964 ? 20 : 22); if ((int16_t)rd16(a) != v) { wr16(a, v); ctl_draw(ch); } return 1; }
    case 0xA966: { uint32_t ch = rd32(sp + 4); uint32_t pt = rd32(sp); setsp(sp + 8); uint32_t c = rd32(ch); Rc r = rc_get(c + 8); int v = (int16_t)(pt >> 16), h = (int16_t)pt; wr16(sp + 8, (rd8(c + 17) != 255 && h >= r.l && h < r.r && v >= r.t && v < r.b) ? 10 : 0); return 1; }
    case 0xA969: { uint32_t wn = rd32(sp); setsp(sp + 4); for (uint32_t ch = rd32(wn + 140); ch; ch = rd32(rd32(ch))) ctl_draw(ch); return 1; }
    case 0xA96D: { uint32_t ch = rd32(sp); setsp(sp + 4); ctl_draw(ch); return 1; }
    case 0xA96C: { uint32_t outp = rd32(sp), wn = rd32(sp + 4), pt = rd32(sp + 8); setsp(sp + 12); uint32_t found = 0; int part = 0; int v = (int16_t)(pt >> 16), h = (int16_t)pt;
        for (uint32_t ch = rd32(wn + 140); ch; ch = rd32(rd32(ch))) { uint32_t c = rd32(ch); Rc r = rc_get(c + 8); if (rd8(c + 16) && rd8(c + 17) != 255 && h >= r.l && h < r.r && v >= r.t && v < r.b) { found = ch; part = 10; break; } }
        wr32(outp, found); wr16(sp + 12, part); return 1; }
    case 0xA968: { uint32_t ch = rd32(sp + 8), pt = rd32(sp + 4); (void)pt; setsp(sp + 12); wr16(sp + 12, 0); (void)ch;
        /* TrackControl: simple modal tracking is not available outside ModalDialog; treat a click as a hit */ wr16(sp + 12, 10); return 1; }

    /* ---- Dialog Manager ---- */
    case 0xA97B: setsp(sp + 4); return 1;      /* InitDialogs */
    case 0xA98B: { for (int i = 0; i < 4; i++) { uint32_t p = rd32(sp + 12 - 4 * i); pstring(p, ptxt[i], 256); } setsp(sp + 16); return 1; }
    case 0xA97C: { uint32_t behind = rd32(sp), storage = rd32(sp + 4); int id = (int16_t)rd16(sp + 8); setsp(sp + 10); uint32_t wn = 0; Res *r = find_res(0x444C4F47, id);
        if (r) { const uint8_t *b = res_bytes(r); Rc bd = {(int16_t)be16(b), (int16_t)be16(b + 2), (int16_t)be16(b + 4), (int16_t)be16(b + 6)}; int proc = (int16_t)be16(b + 8), vis = b[10] != 0, go = b[12] != 0; uint32_t ref = be32(b + 14); int iid = (int16_t)be16(b + 18);
            uint8_t tp[256]; int tl = b[20]; tp[0] = tl; memcpy(tp + 1, b + 21, tl); uint32_t tmp = alloc_block(260); for (int i = 0; i <= tl; i++) wr8(tmp + i, tp[i]);
            Res *ir = find_res(0x4449544C, iid); if (ir) { const uint8_t *ib = res_bytes(ir); wn = dlg_create(storage, bd, tmp, vis, proc, behind, go, ref, ib, slot_len(ir)); } free_block(tmp); }
        wr32(sp + 10, wn); return 1; }
    case 0xA97D: { uint32_t items = rd32(sp), ref = rd32(sp + 4); int go = rd16(sp + 8) != 0; uint32_t behind = rd32(sp + 10); int proc = (int16_t)rd16(sp + 14); int vis = rd16(sp + 16) != 0; uint32_t title = rd32(sp + 18), bounds = rd32(sp + 22), storage = rd32(sp + 26);
        setsp(sp + 30); uint32_t wn = dlg_create(storage, rc_get(bounds), title, vis, proc, behind, go, ref, ram + rd32(items), htext_len(items)); wr32(sp + 30, wn); return 1; }
    case 0xA982: case 0xA983: { uint32_t wn = rd32(sp); setsp(sp + 4); if (n == 0xA983) dlg_dispose(wn); else { Dlg *d = dlg_of(wn); if (d) d->used = 0; wm_dispose(wn, 0); } return 1; }
    case 0xA98A: case 0xA989: setsp(sp + 2); return 1;
    case 0xA98D: { uint32_t boxp = rd32(sp), hp = rd32(sp + 4), tp = rd32(sp + 8); int item = (int16_t)rd16(sp + 12); uint32_t wn = rd32(sp + 14); setsp(sp + 18); Dlg *d = dlg_of(wn);
        if (d && item >= 1 && item <= d->n) { DItem *it = &d->it[item - 1]; wr16(tp, it->type | (it->enabled ? 0 : 0x80)); wr32(hp, it->h); rc_put(boxp, it->box); } return 1; }
    case 0xA98E: { uint32_t boxp = rd32(sp), h = rd32(sp + 4); int ty = (int16_t)rd16(sp + 8), item = (int16_t)rd16(sp + 10); uint32_t wn = rd32(sp + 12); setsp(sp + 16); Dlg *d = dlg_of(wn);
        if (d && item >= 1 && item <= d->n) { DItem *it = &d->it[item - 1]; it->type = ty & 0x7F; it->enabled = !(ty & 0x80); it->h = h; it->box = rc_get(boxp); } return 1; }
    case 0xA98F: { uint32_t h = rd32(sp + 4), t = rd32(sp); setsp(sp + 8); char s[256]; pstring(t, s, 256); htext_set(h, s, strlen(s));
        /* redraw whichever item owns this handle */ for (int i = 0; i < 16; i++) if (dlgs[i].used) for (int k = 0; k < dlgs[i].n; k++) if (dlgs[i].it[k].h == h && win_visible(dlgs[i].rec) && !ddr.active) { uint32_t sv = the_port(); set_the_port(dlgs[i].rec); if (dlgs[i].it[k].type == 16 || dlgs[i].it[k].type == 8) { DC dc; port_dc(dlgs[i].rec, &dc); set_white(&dc); dc_rect(&dc, item_inflate(dlgs[i].it[k].box, dlgs[i].it[k].type == 16 ? 3 : 0)); dc_free(&dc); draw_item(&dlgs[i], k); } set_the_port(sv); }
        return 1; }
    case 0xA990: { uint32_t h = rd32(sp + 4), out = rd32(sp); setsp(sp + 8); char s[256]; htext_get(h, s, 256); int l = strlen(s); wr8(out, l); for (int i = 0; i < l; i++) wr8(out + 1 + i, s[i]); return 1; }
    case 0xA97E: { int item = (int16_t)rd16(sp + 4); uint32_t wn = rd32(sp + 6); setsp(sp + 10); Dlg *d = dlg_of(wn); if (d && item >= 1 && item <= d->n && d->it[item - 1].type == 16) { int old = d->edit; d->edit = item - 1; d->selall = 1; wr16(wn + 164, item - 1); if (win_visible(wn) && !ddr.active) { uint32_t sv = the_port(); set_the_port(wn); if (old >= 0 && old != d->edit) draw_item(d, old); draw_item(d, d->edit); set_the_port(sv); } } return 1; }
    case 0xA981: { uint32_t wn = rd32(sp); if (!ddr.active) { dlg_begin_draw(wn, 0); } if (dlg_draw_step(ret)) return 1; setsp(sp + 4); return 1; }
    case 0xA984: { uint32_t pt = rd32(sp), wn = rd32(sp + 4); setsp(sp + 8); Dlg *d = dlg_of(wn); int ox, oy; win_origin(wn, &ox, &oy); (void)ox; (void)oy; int v = (int16_t)(pt >> 16), h = (int16_t)pt; wr16(sp + 8, d ? (uint16_t)(dlg_find_item(d, h, v)) : 0xFFFF); return 1; }
    case 0xA991: {      /* ModalDialog(filterProc, VAR itemHit) */
        uint32_t filter = rd32(sp + 0 + 4), ihp = rd32(sp);
        if (!md.active) { uint32_t front = 0; for (uint32_t p = win_list_head(); p; p = rd32(p + WR_NEXT)) if (dlg_of(p)) { front = p; break; }
            if (!front) { setsp(sp + 8); wr16(ihp, 0); return 1; }
            md_start(front); dlg_begin_draw(front, 1); }
        if (modal_step(ret, filter)) { wr16(ihp, md.hit); md.active = 0; setsp(sp + 8); return 1; }
        return 1; }
    case 0xA985: case 0xA986: case 0xA987: case 0xA988: {      /* Alert family: (alertID, filterProc): INTEGER */
        uint32_t filter = rd32(sp); int id = (int16_t)rd16(sp + 4);
        if (!al.active) { Res *r = find_res(0x414C5254, id); if (!r) { setsp(sp + 6); wr16(sp + 6, 1); return 1; } const uint8_t *b = res_bytes(r); Rc bd = {(int16_t)be16(b), (int16_t)be16(b + 2), (int16_t)be16(b + 4), (int16_t)be16(b + 6)};
            Res *ir = find_res(0x4449544C, (int16_t)be16(b + 8)); if (!ir) { setsp(sp + 6); wr16(sp + 6, 1); return 1; }
            uint32_t wn = dlg_create(0, bd, 0, 1, 1, 0xFFFFFFFF, 0, 0, res_bytes(ir), slot_len(ir)); if (!wn) { setsp(sp + 6); wr16(sp + 6, 1); return 1; }
            al.active = 1; al.dlg = wn; md_start(wn); dlg_begin_draw(wn, 1); }
        if (modal_step(ret, filter)) { int hit = md.hit; md.active = 0; al.active = 0; dlg_dispose(al.dlg); setsp(sp + 6); wr16(sp + 6, hit); return 1; }
        return 1; }
    case 0xA97F: { uint32_t evp = rd32(sp); setsp(sp + 4); int what = rd16(evp); int r = 0; if (what == 6 || what == 8) r = dlg_of(rd32(evp + 2)) != NULL; else if (what == 1 || what == 3 || what == 5) { uint32_t f = win_front_visible(); r = f && dlg_of(f); } wr_bool(sp + 4, r); return 1; }
    case 0xA980: { uint32_t ihp = rd32(sp), dp = rd32(sp + 4), evp = rd32(sp + 8); setsp(sp + 12); int what = rd16(evp); int r = 0; uint32_t f = win_front_visible(); Dlg *d = f ? dlg_of(f) : NULL;
        if (what == 6) { uint32_t wn = rd32(evp + 2); if (dlg_of(wn)) { win_validate_all(wn); dlg_begin_draw(wn, 1); while (dlg_draw_step(ret)) { } } }
        else if (d && what == 1) { int ox, oy; win_origin(f, &ox, &oy); int i = dlg_find_item(d, (int16_t)rd16(evp + 12) - ox, (int16_t)rd16(evp + 10) - oy); if (i >= 0 && d->it[i].enabled) { wr16(ihp, i + 1); wr32(dp, f); r = 1; } }
        else if (d && (what == 3 || what == 5)) { int ch = rd32(evp + 2) & 0xFF; if (ch == 13 || ch == 3) { wr16(ihp, (int16_t)rd16(f + 168)); wr32(dp, f); r = 1; } else edit_key(d, ch); }
        wr_bool(sp + 12, r); return 1; }
    case 0xA80B: { int popitem = (int16_t)rd16(sp), left = (int16_t)rd16(sp + 2), top = (int16_t)rd16(sp + 4); uint32_t mh = rd32(sp + 6); uint32_t res = 0;
        if (pu_step(ret, mh, top, left, popitem, &res)) { setsp(sp + 10); wr32(sp + 10, res); } return 1; }
    case 0xA827: case 0xA828: {      /* HideDialogItem / ShowDialogItem(dlg, item) */
        int item = (int16_t)rd16(sp); uint32_t wn = rd32(sp + 2); setsp(sp + 6); Dlg *d = dlg_of(wn);
        if (d && item >= 1 && item <= d->n) { DItem *it = &d->it[item - 1]; int hidden = it->box.l >= 8192;
            if (n == 0xA827 && !hidden) { if (win_visible(wn)) { uint32_t sv = the_port(); set_the_port(wn); DC dc; port_dc(wn, &dc); set_white(&dc); dc_rect(&dc, item_inflate(it->box, it->type == 16 ? 3 : (it->type == 4 ? 4 : 0))); dc_free(&dc); set_the_port(sv); }
                it->box.l += 16384; it->box.r += 16384; if (it->type >= 4 && it->type <= 7) { uint32_t c = rd32(it->h); wr8(c + 16, 0); Rc r = rc_get(c + 8); r.l += 16384; r.r += 16384; rc_put(c + 8, r); } }
            else if (n == 0xA828 && hidden) { it->box.l -= 16384; it->box.r -= 16384; if (it->type >= 4 && it->type <= 7) { uint32_t c = rd32(it->h); wr8(c + 16, 255); Rc r = rc_get(c + 8); r.l -= 16384; r.r -= 16384; rc_put(c + 8, r); }
                if (win_visible(wn)) { uint32_t sv = the_port(); set_the_port(wn); draw_item(d, item - 1); set_the_port(sv); } } }
        return 1; }
    default: return 0;
    }
}
