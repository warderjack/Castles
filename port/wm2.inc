/* ---- Window Manager (part 2): window table, frames, creation, z-order, update/activate ---- */
typedef struct { uint32_t w; int proc; uint32_t saved_vis; int alloc; } WInfo;
static WInfo winfo[64]; static int nwinfo = 0;
static WInfo *winfo_of(uint32_t w) { for (int i = 0; i < nwinfo; i++) if (winfo[i].w == w) return &winfo[i]; return NULL; }
static uint32_t active_win = 0;
typedef struct { uint32_t w; int on; } PAct; static PAct pacts[32]; static int npacts = 0;
static void post_activate(uint32_t w, int on) { for (int i = 0; i < npacts; i++) if (pacts[i].w == w) { pacts[i].on = on; return; } if (npacts < 32) { pacts[npacts].w = w; pacts[npacts].on = on; npacts++; } }
static void margins(int proc, int *mt, int *ml, int *mb, int *mr) {
    switch (proc & 0xF) { case 1: *mt = *ml = *mb = *mr = 3; break; case 2: *mt = *ml = *mb = *mr = 1; break; case 3: *mt = *ml = 1; *mb = *mr = 3; break;
    case 0: case 4: case 8: case 12: *mt = 19; *ml = *mb = *mr = 1; break; default: *mt = *ml = *mb = *mr = 1; break; }
}
static void win_set_regions(uint32_t w, Rc cont) {
    WInfo *wi = winfo_of(w); int mt, ml, mb, mr; margins(wi ? wi->proc : 2, &mt, &ml, &mb, &mr);
    rgn_set_rect(rd32(w + WR_CONT), cont); rgn_set_rect(rd32(w + WR_STRUC), (Rc){cont.t - mt, cont.l - ml, cont.b + mb, cont.r + mr});
}
static void draw_window_frame(uint32_t w, const RL *clip) {
    WInfo *wi = winfo_of(w); int proc = wi ? wi->proc : 2; Rc c = win_cont_global(w); int active = rd8(w + WR_HILITED);
    DC d; screen_dc(&d, clip); int blk = rgb_px(&d.s, (RGB){0, 0, 0}), wht = rgb_px(&d.s, (RGB){0xFFFF, 0xFFFF, 0xFFFF}); (void)blk; (void)wht;
    int mt, ml, mb, mr; margins(proc, &mt, &ml, &mb, &mr); Rc st = {c.t - mt, c.l - ml, c.b + mb, c.r + mr};
    switch (proc & 0xF) {
    case 1: frame_rect_dc(&d, st, 1); frame_rect_dc(&d, (Rc){st.t + 1, st.l + 1, st.b - 1, st.r - 1}, 0); frame_rect_dc(&d, (Rc){st.t + 2, st.l + 2, st.b - 2, st.r - 2}, 1); break;
    case 2: frame_rect_dc(&d, st, 1); break;
    case 3: { Rc f = {st.t, st.l, st.b - 2, st.r - 2}; frame_rect_dc(&d, f, 1); fill_rect_dc(&d, (Rc){f.t + 2, f.r, f.b + 2, f.r + 2}, 1); fill_rect_dc(&d, (Rc){f.b, f.l + 2, f.b + 2, f.r + 2}, 1); break; }
    case 0: case 4: case 8: case 12: {
        frame_rect_dc(&d, st, 1); Rc bar = {st.t + 1, st.l + 1, c.t - 1, st.r - 1}; fill_rect_dc(&d, bar, 0);
        fill_rect_dc(&d, (Rc){c.t - 1, st.l, c.t, st.r}, 1);
        if (active) for (int y = bar.t + 2; y < bar.b - 2; y += 2) fill_rect_dc(&d, (Rc){y, bar.l + 2, y + 1, bar.r - 2}, 1);
        uint32_t th = rd32(w + WR_TITLE); char t[256]; int l = 0; if (th && rd32(th)) { l = rd8(rd32(th)); for (int i = 0; i < l; i++) t[i] = rd8(rd32(th) + 1 + i); } t[l] = 0;
        int tw = text_w(t, 0, 12, 1) + 12, tx = (bar.l + bar.r) / 2 - tw / 2; if (l) { fill_rect_dc(&d, (Rc){bar.t, tx, bar.b, tx + tw}, 0); text_at(&d, tx + 6, bar.b - 5, t, 0, 12, 1); }
        if (rd8(w + WR_GOAWAY) && active) { Rc g = {bar.t + 3, bar.l + 7, bar.t + 14, bar.l + 18}; fill_rect_dc(&d, (Rc){g.t - 1, g.l - 1, g.b + 1, g.r + 1}, 0); frame_rect_dc(&d, g, 1); }
        break; }
    default: frame_rect_dc(&d, st, 1); break;
    }
    dc_free(&d);
}
static void wm_activate_front(void) {
    uint32_t f = win_front_visible(); if (f == active_win) return;
    if (active_win && is_window(active_win)) { wr8(active_win + WR_HILITED, 0); post_activate(active_win, 0); }
    active_win = f; if (f) { wr8(f + WR_HILITED, 1); post_activate(f, 1); }
}
static uint32_t wm_make_window(uint32_t storage, Rc bounds, uint32_t title, int visible, int proc, uint32_t behind, int goaway, uint32_t refcon, int color) {
    if (nwinfo >= 64) return 0;
    int alloc = 0; if (!storage) { storage = alloc_block(160); alloc = 1; } if (!storage) return 0; memset(ram + storage, 0, 160);
    int w = bounds.r - bounds.l, h = bounds.b - bounds.t;
    uint32_t pmh = 0;
    if (color) {
        pmh = new_handle(50); memcpy(ram + rd32(pmh), ram + rd32(main_pm), 50); rc_put(rd32(pmh) + 6, (Rc){-bounds.t, -bounds.l, SCR_H - bounds.t, SCR_W - bounds.l});
        setup_cport(storage, pmh, (Rc){0, 0, h, w});
    } else {
        init_port_fields(storage, 0, (Rc){0, 0, h, w}); wr32(storage + 2, FB_BASE); wr16(storage + 6, SCR_W); rc_put(storage + 8, (Rc){-bounds.t, -bounds.l, SCR_H - bounds.t, SCR_W - bounds.l});
    }
    wr32(storage + 24, new_rgn()); rgn_set_rect(rd32(storage + 24), (Rc){0, 0, h, w});
    wr16(storage + 108, 8); wr8(storage + WR_VISIBLE, 0); wr8(storage + WR_HILITED, 0); wr8(storage + WR_GOAWAY, goaway != 0);
    wr32(storage + WR_STRUC, new_rgn()); wr32(storage + WR_CONT, new_rgn()); wr32(storage + WR_UPDATE, new_rgn()); wr32(storage + WR_REF, refcon);
    uint32_t th = new_handle(256); uint32_t tl = title ? rd8(title) : 0; wr8(rd32(th), tl); for (uint32_t i = 0; i < tl; i++) wr8(rd32(th) + 1 + i, rd8(title + 1 + i)); wr32(storage + WR_TITLE, th);
    wr32(storage + WR_NEXT, 0);
    winfo[nwinfo].w = storage; winfo[nwinfo].proc = proc; winfo[nwinfo].saved_vis = 0; winfo[nwinfo].alloc = alloc; nwinfo++;
    win_set_regions(storage, bounds);
    /* link into list */
    if (behind == 0xFFFFFFFF || !win_list_head()) { wr32(storage + WR_NEXT, win_list_head()); wr32(0x9D6, storage); }
    else if (behind == 0) { uint32_t p = win_list_head(); while (rd32(p + WR_NEXT)) p = rd32(p + WR_NEXT); wr32(p + WR_NEXT, storage); }
    else { wr32(storage + WR_NEXT, rd32(behind + WR_NEXT)); wr32(behind + WR_NEXT, storage); }
    if (visible) { WM_SNAP_BEGIN(); wr8(storage + WR_VISIBLE, 1); WM_SNAP_END(); wm_activate_front(); draw_desktop_and_frames(); }
    else win_apply_vis(storage);
    return storage;
}
static void wm_remove(uint32_t w) {
    if (win_list_head() == w) wr32(0x9D6, rd32(w + WR_NEXT)); else for (uint32_t p = win_list_head(); p; p = rd32(p + WR_NEXT)) if (rd32(p + WR_NEXT) == w) { wr32(p + WR_NEXT, rd32(w + WR_NEXT)); break; }
    wr32(w + WR_NEXT, 0);
}
static void wm_show_hide(uint32_t w, int show) {
    if ((rd8(w + WR_VISIBLE) != 0) == (show != 0)) return;
    WM_SNAP_BEGIN(); wr8(w + WR_VISIBLE, show ? 1 : 0);
    if (show) { RL u = {0}; Rc c = win_cont_global(w); int ox, oy; win_origin(w, &ox, &oy); Rc lc = {c.t - oy, c.l - ox, c.b - oy, c.r - ox}; rl_add(&u, lc); rgn_set(rd32(w + WR_UPDATE), &u); rl_free(&u); }
    else { if (w == active_win) { wr8(w + WR_HILITED, 0); post_activate(w, 0); active_win = 0; } }
    WM_SNAP_END(); wm_activate_front(); draw_desktop_and_frames();
}
static void wm_bring_to_front(uint32_t w) {
    if (win_list_head() == w) return; WM_SNAP_BEGIN(); wm_remove(w); wr32(w + WR_NEXT, win_list_head()); wr32(0x9D6, w); WM_SNAP_END(); wm_activate_front(); draw_desktop_and_frames();
}
static void wm_send_behind(uint32_t w, uint32_t behind) {
    WM_SNAP_BEGIN(); wm_remove(w);
    if (!behind) { uint32_t p = win_list_head(); if (!p) { wr32(0x9D6, w); } else { while (rd32(p + WR_NEXT)) p = rd32(p + WR_NEXT); wr32(p + WR_NEXT, w); } }
    else { wr32(w + WR_NEXT, rd32(behind + WR_NEXT)); wr32(behind + WR_NEXT, w); }
    WM_SNAP_END(); wm_activate_front(); draw_desktop_and_frames();
}
static void wm_move(uint32_t w, int h, int v) {
    Rc c = win_cont_global(w); int dx = h - c.l, dy = v - c.t; if (!dx && !dy) return; WM_SNAP_BEGIN();
    RL a = {0}; rgn_get(rd32(w + WR_CONT), &a); rl_offset(&a, dx, dy); rgn_set(rd32(w + WR_CONT), &a); rl_free(&a);
    RL b = {0}; rgn_get(rd32(w + WR_STRUC), &b); rl_offset(&b, dx, dy); rgn_set(rd32(w + WR_STRUC), &b); rl_free(&b);
    uint32_t bm = port_bnd_addr(w); Rc nb = rc_get(bm); nb.t -= dy; nb.b -= dy; nb.l -= dx; nb.r -= dx; rc_put(bm, nb);
    /* moved content must be redrawn */ { Rc pr = rc_get(w + 16); RL u = {0}; rl_add(&u, pr); rgn_set(rd32(w + WR_UPDATE), &u); rl_free(&u); }
    WM_SNAP_END(); draw_desktop_and_frames();
}
static void wm_size(uint32_t w, int ww, int hh) {
    Rc c = win_cont_global(w); c.r = c.l + ww; c.b = c.t + hh; WM_SNAP_BEGIN(); win_set_regions(w, c); rc_put(w + 16, (Rc){0, 0, hh, ww});
    RL u = {0}; rl_add(&u, (Rc){0, 0, hh, ww}); rgn_set(rd32(w + WR_UPDATE), &u); rl_free(&u); WM_SNAP_END();
}
static void wm_dispose(uint32_t w, int freeit) {
    if (!is_window(w)) return; WM_SNAP_BEGIN();
    if (w == active_win) { active_win = 0; }
    wr8(w + WR_VISIBLE, 0); wm_remove(w); WM_SNAP_END(); wm_activate_front(); draw_desktop_and_frames();
    for (int i = 0; i < npacts; i++) if (pacts[i].w == w) { pacts[i] = pacts[--npacts]; i--; }
    WInfo *wi = winfo_of(w); int alloc = wi ? wi->alloc : 0; if (wi) *wi = winfo[--nwinfo];
    dispose_handle(rd32(w + WR_TITLE)); dispose_handle(rd32(w + WR_STRUC)); dispose_handle(rd32(w + WR_CONT)); dispose_handle(rd32(w + WR_UPDATE)); dispose_handle(rd32(w + 24));
    if (freeit && alloc) free_block(w);
}
static void inval_rgn_local(uint32_t w, const RL *l, int add) {
    if (!is_window(w)) return; RL u = {0}, t = {0}; rgn_get(rd32(w + WR_UPDATE), &u);
    if (add) { RL vis = {0}; win_vis_global(w, &vis); int ox, oy; win_origin(w, &ox, &oy); rl_offset(&vis, -ox, -oy); RL li = {0}; rl_sect(l, &vis, &li); rl_union(&u, &li, &t); rl_free(&vis); rl_free(&li); }
    else rl_diff(&u, l, &t);
    rgn_set(rd32(w + WR_UPDATE), &t); rl_free(&u); rl_free(&t);
}
static int find_window_part(int gx, int gy, uint32_t *which) {
    *which = 0; if (gy < mbar_height) return 1;
    for (uint32_t p = win_list_head(); p; p = rd32(p + WR_NEXT)) {
        if (!win_visible(p)) continue; RL s = {0}, c = {0}; rgn_get(rd32(p + WR_STRUC), &s); rgn_get(rd32(p + WR_CONT), &c);
        int in_s = rl_contains(&s, gx, gy), in_c = rl_contains(&c, gx, gy); rl_free(&s); rl_free(&c);
        if (in_c) { *which = p; return 3; }
        if (in_s) { *which = p; WInfo *wi = winfo_of(p); int pr = wi ? wi->proc : 2; Rc cc = win_cont_global(p);
            if ((pr & 0xF) == 0 || (pr & 0xF) == 4 || (pr & 0xF) == 8 || (pr & 0xF) == 12) { if (gy < cc.t) { if (rd8(p + WR_GOAWAY) && gx >= cc.l + 6 && gx < cc.l + 19 && gy >= cc.t - 16 && gy < cc.t - 4) return 6; return 4; } }
            return 4; }
    }
    return 0;
}
