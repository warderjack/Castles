/* ---- Event Manager, Window Manager, Menu Manager ---- */
static int wm_redirected = 0;               /* set when a handler rewound PC (blocking trap re-executes next slice) */
static int idle_hint = 0;
static uint32_t trap_pc = 0; static int trap_from_stub = 0;
static void rewind_trap(uint32_t ret) { if (trap_from_stub) push32(ret); W(M68K_REG_PC, trap_pc); wm_redirected = 1; m68k_end_timeslice(); }

/* ---------- region helpers ---------- */
static void rl_offset(RL *l, int dx, int dy) { for (int i = 0; i < l->n; i++) { l->r[i].l += dx; l->r[i].r += dx; l->r[i].t += dy; l->r[i].b += dy; } }
static int rl_isempty(const RL *l) { for (int i = 0; i < l->n; i++) if (!rc_empty(l->r[i])) return 0; return 1; }
static void rl_copy(const RL *a, RL *out) { for (int i = 0; i < a->n; i++) rl_add(out, a->r[i]); }
static void rgn_copy_to(uint32_t dst, const RL *l) { rgn_set(dst, l); }
static int rl_contains(const RL *l, int x, int y) { for (int i = 0; i < l->n; i++) if (x >= l->r[i].l && x < l->r[i].r && y >= l->r[i].t && y < l->r[i].b) return 1; return 0; }

/* ---------- events ---------- */
typedef struct { int what; uint32_t msg, when; int v, h, mods; } MEvt;
static MEvt evq[256]; static int evq_h = 0, evq_n = 0;
static uint32_t cur_mods(void) { return plat_mods | (plat_btn ? 0 : 0x80); }
static void ev_post(int what, uint32_t msg, int mods) {
    if (evq_n == 256) { evq_h = (evq_h + 1) & 255; evq_n--; }
    MEvt *e = &evq[(evq_h + evq_n) & 255]; e->what = what; e->msg = msg; e->when = ticks_now(); e->v = plat_my; e->h = plat_mx; e->mods = mods; evq_n++;
}
static int quit_requested = 0, swallow_up = 0;
static void ev_pump(void) {
    PEvt e; int guard = 0;
    while (guard++ < 200 && plat_poll(&e, ticks_now())) {
        switch (e.type) {
        case PE_QUIT: quit_requested = 1; break;
        case PE_DOWN: ev_post(1, 0, plat_mods); break;
        case PE_UP: if (swallow_up) { swallow_up = 0; break; } ev_post(2, 0, plat_mods | 0x80); break;
        case PE_KEY: ev_post(3, (e.key << 8) | (e.chr & 0xFF), e.mods); break;
        case PE_AUTOKEY: ev_post(5, (e.key << 8) | (e.chr & 0xFF), e.mods); break;
        case PE_KEYUP: ev_post(4, (e.key << 8) | (e.chr & 0xFF), e.mods); break;
        default: break;
        }
    }
    wr16(0x82C, plat_my); wr16(0x82E, plat_mx); wr16(0x830, plat_my); wr16(0x832, plat_mx); memcpy(ram + 0x174, keymap_state, 16);
}

/* ---------- windows ---------- */
#define WR_VISIBLE 110
#define WR_HILITED 111
#define WR_GOAWAY 112
#define WR_STRUC 114
#define WR_CONT 118
#define WR_UPDATE 122
#define WR_PROC 126
#define WR_DATA 130
#define WR_TITLE 134
#define WR_TITLEW 138
#define WR_CTLS 140
#define WR_NEXT 144
#define WR_PIC 148
#define WR_REF 152
static uint32_t win_list_head(void) { return rd32(0x9D6); }
static int is_window(uint32_t w) { for (uint32_t p = win_list_head(); p; p = rd32(p + WR_NEXT)) if (p == w) return 1; return 0; }
static int win_visible(uint32_t w) { return rd8(w + WR_VISIBLE) != 0; }
static int win_kind_dialog(uint32_t w) { return (int16_t)rd16(w + 108) == 2; }
static uint32_t win_front_visible(void) { for (uint32_t p = win_list_head(); p; p = rd32(p + WR_NEXT)) if (win_visible(p)) return p; return 0; }
static uint32_t port_bnd_addr(uint32_t port) { return port_is_color(port) ? rd32(rd32(port + 2)) + 6 : port + 8; }
static void win_origin(uint32_t w, int *gx, int *gy) { Rc b = rc_get(port_bnd_addr(w)); *gx = -b.l; *gy = -b.t; }       /* global position of local (0,0) */
static Rc win_cont_global(uint32_t w) { return rc_get(rd32(w + WR_CONT) ? rd32(rd32(w + WR_CONT)) + 2 : 0); }
static uint32_t gray_rgn = 0; static int mbar_height = 20;
static uint32_t desk_port = 0;
static uint32_t menu_bar_rgn_dirty = 0;

/* global visible content region of w = cont - (struc of windows in front of w) - menubar */
static void win_vis_global(uint32_t w, RL *out) {
    RL cont = {0}; rgn_get(rd32(w + WR_CONT), &cont); RL cur = {0}; rl_copy(&cont, &cur); rl_free(&cont);
    for (uint32_t p = win_list_head(); p && p != w; p = rd32(p + WR_NEXT)) {
        if (!win_visible(p)) continue; RL s = {0}, t = {0}; rgn_get(rd32(p + WR_STRUC), &s); rl_diff(&cur, &s, &t); rl_free(&s); rl_free(&cur); cur = t;
    }
    RL scr = {0}, t2 = {0}; rl_add(&scr, (Rc){mbar_height, 0, SCR_H, SCR_W}); rl_sect(&cur, &scr, &t2); rl_free(&scr); rl_free(&cur); rl_copy(&t2, out); rl_free(&t2);
}
static void win_apply_vis(uint32_t w) {       /* set port visRgn (local coords) from global visibility */
    RL g = {0}; if (win_visible(w)) win_vis_global(w, &g); int ox, oy; win_origin(w, &ox, &oy); rl_offset(&g, -ox, -oy); rgn_set(rd32(w + 24), &g); rl_free(&g);
}
static void wm_snapshot(RL *vis_out, uint32_t *wins, int *nw) {      /* per-window global visible region, list order */
    int n = 0; for (uint32_t p = win_list_head(); p && n < 64; p = rd32(p + WR_NEXT)) { wins[n] = p; memset(&vis_out[n], 0, sizeof(RL)); if (win_visible(p)) win_vis_global(p, &vis_out[n]); n++; } *nw = n;
}
static void draw_desktop_and_frames(void);
static void wm_changed(RL *before, uint32_t *bw, int nb) {            /* after a z-order/visibility change: invalidate newly exposed parts, redraw frames/desktop */
    for (uint32_t p = win_list_head(); p; p = rd32(p + WR_NEXT)) {
        RL now = {0}; if (win_visible(p)) win_vis_global(p, &now);
        RL old = {0}; for (int i = 0; i < nb; i++) if (bw[i] == p) { old = before[i]; break; }
        RL added = {0}; rl_diff(&now, &old, &added);
        if (!rl_isempty(&added)) { int ox, oy; win_origin(p, &ox, &oy); rl_offset(&added, -ox, -oy); RL u = {0}, t = {0}; rgn_get(rd32(p + WR_UPDATE), &u); rl_union(&u, &added, &t); rgn_set(rd32(p + WR_UPDATE), &t); rl_free(&u); rl_free(&t); }
        rl_free(&added); rl_free(&now); win_apply_vis(p);
    }
    for (int i = 0; i < nb; i++) rl_free(&before[i]);
    draw_desktop_and_frames();
}
#define WM_SNAP_BEGIN() RL _snap[64]; uint32_t _snw[64]; int _snn = 0; wm_snapshot(_snap, _snw, &_snn)
#define WM_SNAP_END() wm_changed(_snap, _snw, _snn)

static void screen_dc(DC *d, const RL *clip) {
    dc_init(d, screen_port); rl_free(&d->clip); d->clip = (RL){0}; RL b = {0}; rl_add(&b, (Rc){0, 0, SCR_H, SCR_W}); rl_sect(clip, &b, &d->clip); rl_free(&b);
    d->s.bnd = (Rc){0, 0, SCR_H, SCR_W};
}
static void text_at(DC *d, int x, int y, const char *str, int font, int size, int face) {
    uint32_t port = d->port; uint16_t sf = rd16(port + 68), ss = rd16(port + 74); uint8_t sfa = rd8(port + 70); uint16_t sm = rd16(port + 72); uint32_t pl = rd32(port + 48);
    wr16(port + 68, font); wr16(port + 74, size); wr8(port + 70, face); wr16(port + 72, 1); wr16(port + 48, y); wr16(port + 50, x);
    draw_text(d, (const uint8_t *)str, strlen(str)); wr16(port + 68, sf); wr16(port + 74, ss); wr8(port + 70, sfa); wr16(port + 72, sm); wr32(port + 48, pl);
}
static int text_w(const char *str, int font, int size, int face) {
    uint32_t port = screen_port; uint16_t sf = rd16(port + 68), ss = rd16(port + 74); uint8_t sfa = rd8(port + 70); wr16(port + 68, font); wr16(port + 74, size); wr8(port + 70, face);
    int w = str_width(port, (const uint8_t *)str, strlen(str)); wr16(port + 68, sf); wr16(port + 74, ss); wr8(port + 70, sfa); return w;
}
static void fill_rect_dc(DC *d, Rc r, int black) { uint8_t b[8]; memset(b, black ? 0xFF : 0, 8); dc_set_pat8(d, b); d->mode = 8; dc_rect(d, r); }
static void frame_rect_dc(DC *d, Rc r, int black) {
    fill_rect_dc(d, (Rc){r.t, r.l, r.t + 1, r.r}, black); fill_rect_dc(d, (Rc){r.b - 1, r.l, r.b, r.r}, black);
    fill_rect_dc(d, (Rc){r.t, r.l, r.b, r.l + 1}, black); fill_rect_dc(d, (Rc){r.t, r.r - 1, r.b, r.r}, black);
}
static void draw_window_frame(uint32_t w, const RL *clip_global);
static uint32_t win_struc_visible_clip(uint32_t w, RL *out) {            /* struc - cont - fronts */
    RL s = {0}, c = {0}, t = {0}; rgn_get(rd32(w + WR_STRUC), &s); rgn_get(rd32(w + WR_CONT), &c); rl_diff(&s, &c, &t); rl_free(&s); rl_free(&c);
    for (uint32_t p = win_list_head(); p && p != w; p = rd32(p + WR_NEXT)) { if (!win_visible(p)) continue; RL f = {0}, t2 = {0}; rgn_get(rd32(p + WR_STRUC), &f); rl_diff(&t, &f, &t2); rl_free(&f); rl_free(&t); t = t2; }
    RL scr = {0}, t3 = {0}; rl_add(&scr, (Rc){mbar_height, 0, SCR_H, SCR_W}); rl_sect(&t, &scr, &t3); rl_free(&scr); rl_free(&t); rl_copy(&t3, out); rl_free(&t3); return 0;
}
static void menubar_draw(void);
static void draw_desktop_and_frames(void) {
    /* desktop = screen below menubar minus all visible struc regions */
    RL desk = {0}; rl_add(&desk, (Rc){mbar_height, 0, SCR_H, SCR_W});
    for (uint32_t p = win_list_head(); p; p = rd32(p + WR_NEXT)) { if (!win_visible(p)) continue; RL s = {0}, t = {0}; rgn_get(rd32(p + WR_STRUC), &s); rl_diff(&desk, &s, &t); rl_free(&s); rl_free(&desk); desk = t; }
    DC d; screen_dc(&d, &desk); static const uint8_t gray[8] = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55}; dc_set_pat8(&d, gray); d.mode = 8;
    d.fg = rgb_px(&d.s, (RGB){0x4444, 0x4444, 0x6666}); d.bg = rgb_px(&d.s, (RGB){0x2222, 0x2222, 0x4444}); dc_set_pat8(&d, gray);
    for (int i = 0; i < desk.n; i++) dc_rect(&d, desk.r[i]); dc_free(&d); rl_free(&desk);
    for (uint32_t p = win_list_head(); p; p = rd32(p + WR_NEXT)) if (win_visible(p)) { RL c = {0}; win_struc_visible_clip(p, &c); if (!rl_isempty(&c)) draw_window_frame(p, &c); rl_free(&c); }
    menubar_draw();
    gfx_dirty = 1;
}
