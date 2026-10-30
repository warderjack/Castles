/* ---- Event/Window/Menu trap dispatcher ---- */
static void put_event(uint32_t evp, int what, uint32_t msg, uint32_t when, int v, int h, int mods) { wr16(evp, what); wr32(evp + 2, msg); wr32(evp + 6, when); wr16(evp + 10, v); wr16(evp + 12, h); wr16(evp + 14, mods); }
static int get_event(uint32_t evp, int mask, int remove) {
    ev_pump();
    for (int i = 0; i < evq_n; i++) { MEvt *e = &evq[(evq_h + i) & 255]; if (mask & (1 << e->what)) {
        put_event(evp, e->what, e->msg, e->when, e->v, e->h, e->mods);
        if (remove) { for (int j = i; j + 1 < evq_n; j++) evq[(evq_h + j) & 255] = evq[(evq_h + j + 1) & 255]; evq_n--; }
        return 1; } }
    if ((mask & (1 << 8)) && npacts) { PAct a = pacts[0]; put_event(evp, 8, a.w, ticks_now(), plat_my, plat_mx, (cur_mods() & ~1) | (a.on ? 1 : 0)); if (remove) { memmove(&pacts[0], &pacts[1], (npacts - 1) * sizeof(PAct)); npacts--; } return 1; }
    if (mask & (1 << 6)) for (uint32_t p = win_list_head(); p; p = rd32(p + WR_NEXT)) {
        if (!win_visible(p)) continue; RL u = {0}; rgn_get(rd32(p + WR_UPDATE), &u); int ne = !rl_isempty(&u); rl_free(&u);
        if (ne) { put_event(evp, 6, p, ticks_now(), plat_my, plat_mx, cur_mods()); return 1; } }
    put_event(evp, 0, 0, ticks_now(), plat_my, plat_mx, cur_mods()); return 0;
}
static uint32_t wind_title_default;
static int delay_active = 0; static uint32_t delay_until = 0;
static int wm_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    uint32_t sp = SP;
    switch (n) {
    /* ---------------- events ---------------- */
    case 0xA970: case 0xA971: { uint32_t evp = rd32(sp); int mask = rd16(sp + 4); int r = get_event(evp, mask, n == 0xA970); setsp(sp + 6); wr_bool(sp + 6, r); if (!r) idle_hint = 1; return 1; }
    case 0xA860: { uint32_t evp = rd32(sp + 8); int mask = rd16(sp + 12); uint32_t sleep = rd32(sp + 4); int r = get_event(evp, mask, 1);
        { static int wn; if (getenv("WNEDBG") && (wn++ % 20000) == 0) { fprintf(stderr, "[wne] mask=%04x r=%d what=%d front=%06x active=%06x\n", mask, r, rd16(evp), win_list_head(), active_win); for (uint32_t p = win_list_head(); p; p = rd32(p + WR_NEXT)) { RL u = {0}; rgn_get(rd32(p + WR_UPDATE), &u); fprintf(stderr, "   win %06x vis=%d hil=%d upd_empty=%d\n", p, win_visible(p), rd8(p + WR_HILITED), rl_isempty(&u)); rl_free(&u); } } } setsp(sp + 14); wr_bool(sp + 14, r); if (!r && sleep) idle_hint = 1; return 1; }
    case 0xA974: { ev_pump(); wr_bool(sp, plat_btn); return 1; }
    case 0xA973: { ev_pump(); wr_bool(sp, plat_btn); if (plat_btn) idle_hint = 1; return 1; }
    case 0xA977: { ev_pump(); if (plat_btn) { rewind_trap(ret); return 1; } for (int i = 0; i < evq_n; i++) if (evq[(evq_h + i) & 255].what == 2) { for (int j = i; j + 1 < evq_n; j++) evq[(evq_h + j) & 255] = evq[(evq_h + j + 1) & 255]; evq_n--; break; } wr_bool(sp, 0); return 1; }
    case 0xA972: { uint32_t p = rd32(sp); setsp(sp + 4); ev_pump(); uint32_t port = the_port(); int ox = 0, oy = 0; if (port) { Rc b = rc_get(port_bnd_addr(port)); ox = b.l; oy = b.t; } wr16(p, plat_my + oy); wr16(p + 2, plat_mx + ox); return 1; }
    case 0xA976: { uint32_t p = rd32(sp); setsp(sp + 4); ev_pump(); memcpy(ram + p, keymap_state, 16); return 1; }
    case 0xA9B4: ev_pump(); return 1;                                                       /* SystemTask */
    case 0xA032: { uint32_t m = D(0) & 0xFFFF; for (int i = 0; i < evq_n; i++) if (m & (1 << evq[(evq_h + i) & 255].what)) { for (int j = i; j + 1 < evq_n; j++) evq[(evq_h + j) & 255] = evq[(evq_h + j + 1) & 255]; evq_n--; i--; } SETD(0, 0); return 1; }   /* FlushEvents */
    case 0xA03B: {      /* Delay(ticks) */
        if (!delay_active) { delay_active = 1; delay_until = ticks_now() + D(0); }
        ev_pump(); if (ticks_now() < delay_until) { idle_hint = 1; rewind_trap(ret); return 1; }
        delay_active = 0; SETD(0, ticks_now()); return 1; }
    /* ---------------- Window Manager ---------------- */
    case 0xA912: { rc_put(0x6C, (Rc){0, 0, SCR_H, SCR_W}); wr16(0xBAA, 20); wr32(0x9D6, 0); if (screen_port) draw_desktop_and_frames(); return 1; }     /* InitWindows */
    case 0xA910: { uint32_t p = rd32(sp); setsp(sp + 4); wr32(p, screen_port); return 1; }                                  /* GetWMgrPort */
    case 0xA913: case 0xAA45: {      /* NewWindow / NewCWindow */
        uint32_t refcon = rd32(sp); int goaway = rd16(sp + 4) != 0; uint32_t behind = rd32(sp + 6); int proc = (int16_t)rd16(sp + 10); int vis = rd16(sp + 12) != 0; uint32_t title = rd32(sp + 14), bounds = rd32(sp + 18), storage = rd32(sp + 22);
        setsp(sp + 26); uint32_t wn = wm_make_window(storage, rc_get(bounds), title, vis, proc, behind, goaway, refcon, n == 0xAA45); wr32(sp + 26, wn); return 1; }
    case 0xA9BD: case 0xAA46: {      /* GetNewWindow / GetNewCWindow (id, storage, behind) */
        uint32_t behind = rd32(sp); uint32_t storage = rd32(sp + 4); int id = (int16_t)rd16(sp + 8); setsp(sp + 10);
        Res *r = find_res(0x57494E44, id); uint32_t wn = 0;
        if (r) { const uint8_t *d = r->buf + r->off; Rc b = {(int16_t)be16(d), (int16_t)be16(d + 2), (int16_t)be16(d + 4), (int16_t)be16(d + 6)}; int proc = (int16_t)be16(d + 8); int vis = d[10] != 0; int go = d[12] != 0; uint32_t ref = be32(d + 14);
            uint32_t tp = alloc_block(260); wr8(tp, d[18]); for (int i = 0; i < d[18]; i++) wr8(tp + 1 + i, d[19 + i]);
            wn = wm_make_window(storage, b, tp, vis, proc, behind, go, ref, n == 0xAA46); free_block(tp); }
        wr32(sp + 10, wn); return 1; }
    case 0xA915: { uint32_t wn = rd32(sp); setsp(sp + 4); if (is_window(wn)) wm_show_hide(wn, 1); return 1; }
    case 0xA916: { uint32_t wn = rd32(sp); setsp(sp + 4); if (is_window(wn)) wm_show_hide(wn, 0); return 1; }
    case 0xA908: { int show = rd16(sp) != 0; uint32_t wn = rd32(sp + 2); setsp(sp + 6); if (is_window(wn)) wm_show_hide(wn, show); return 1; }
    case 0xA91F: { uint32_t wn = rd32(sp); setsp(sp + 4); if (is_window(wn)) { if (!win_visible(wn)) wm_show_hide(wn, 1); wm_bring_to_front(wn); } return 1; }           /* SelectWindow */
    case 0xA920: { uint32_t wn = rd32(sp); setsp(sp + 4); if (is_window(wn)) wm_bring_to_front(wn); return 1; }
    case 0xA921: { uint32_t b = rd32(sp), wn = rd32(sp + 4); setsp(sp + 8); if (is_window(wn)) wm_send_behind(wn, b); return 1; }
    case 0xA91C: { int on = rd16(sp) != 0; uint32_t wn = rd32(sp + 2); setsp(sp + 6); if (is_window(wn)) { wr8(wn + WR_HILITED, on); draw_desktop_and_frames(); } return 1; }
    case 0xA924: { setsp(sp); wr32(sp, win_front_visible()); return 1; }
    case 0xA91B: { int v = (int16_t)rd16(sp + 2), h = (int16_t)rd16(sp + 4); uint32_t wn = rd32(sp + 6); int front = rd16(sp) != 0; setsp(sp + 10); if (is_window(wn)) { wm_move(wn, h, v); if (front) wm_bring_to_front(wn); } return 1; }
    case 0xA91D: { int hh = (int16_t)rd16(sp + 2), ww = (int16_t)rd16(sp + 4); uint32_t wn = rd32(sp + 6); setsp(sp + 10); if (is_window(wn)) wm_size(wn, ww, hh); return 1; }
    case 0xA914: { uint32_t wn = rd32(sp); setsp(sp + 4); wm_dispose(wn, 1); return 1; }
    case 0xA92D: { uint32_t wn = rd32(sp); setsp(sp + 4); wm_dispose(wn, 0); return 1; }
    case 0xA917: { uint32_t wn = rd32(sp); setsp(sp + 4); wr32(sp + 4, wn ? rd32(wn + WR_REF) : 0); return 1; }
    case 0xA918: { uint32_t v = rd32(sp), wn = rd32(sp + 4); setsp(sp + 8); if (wn) wr32(wn + WR_REF, v); return 1; }
    case 0xA91A: { uint32_t t = rd32(sp), wn = rd32(sp + 4); setsp(sp + 8); if (is_window(wn) && t) { uint32_t th = rd32(wn + WR_TITLE); int l = rd8(t); wr8(rd32(th), l); for (int i = 0; i < l; i++) wr8(rd32(th) + 1 + i, rd8(t + 1 + i)); draw_desktop_and_frames(); } return 1; }
    case 0xA919: { uint32_t t = rd32(sp), wn = rd32(sp + 4); setsp(sp + 8); if (wn) { uint32_t th = rd32(wn + WR_TITLE); int l = rd8(rd32(th)); for (int i = 0; i <= l; i++) wr8(t + i, rd8(rd32(th) + i)); } return 1; }
    case 0xA922: {      /* BeginUpdate */
        uint32_t wn = rd32(sp); setsp(sp + 4); WInfo *wi = winfo_of(wn); if (!wi) return 1;
        uint32_t oldv = rd32(wn + 24); wi->saved_vis = oldv; uint32_t nv = new_rgn(); RL a = {0}, b = {0}, c = {0}; rgn_get(oldv, &a); rgn_get(rd32(wn + WR_UPDATE), &b); rl_sect(&a, &b, &c); rgn_set(nv, &c); rl_free(&a); rl_free(&b); rl_free(&c);
        wr32(wn + 24, nv); return 1; }
    case 0xA923: {      /* EndUpdate */
        uint32_t wn = rd32(sp); setsp(sp + 4); WInfo *wi = winfo_of(wn); if (!wi || !wi->saved_vis) return 1;
        dispose_handle(rd32(wn + 24)); wr32(wn + 24, wi->saved_vis); wi->saved_vis = 0; RL e = {0}; rgn_set(rd32(wn + WR_UPDATE), &e); return 1; }
    case 0xA928: case 0xA92A: {      /* InvalRect / ValidRect (local rect) */
        Rc r = rc_get(rd32(sp)); setsp(sp + 4); uint32_t port = the_port(); if (!port) return 1; RL l = {0}; rl_add(&l, r); inval_rgn_local(port, &l, n == 0xA928); rl_free(&l); return 1; }
    case 0xA927: case 0xA929: {      /* InvalRgn / ValidRgn */
        uint32_t rg = rd32(sp); setsp(sp + 4); uint32_t port = the_port(); if (!port) return 1; RL l = {0}; rgn_get(rg, &l); inval_rgn_local(port, &l, n == 0xA927); rl_free(&l); return 1; }
    case 0xA92C: {      /* FindWindow(thePt, VAR whichWindow): Integer */
        uint32_t wp = rd32(sp); int v = (int16_t)rd16(sp + 4), h = (int16_t)rd16(sp + 6); setsp(sp + 8); uint32_t wn; int part = find_window_part(h, v, &wn); wr32(wp, wn); wr16(sp + 8, part); return 1; }
    case 0xA91E: { setsp(sp + 8); wr_bool(sp + 8, 0); return 1; }                       /* TrackGoAway -> false (not implemented) */
    case 0xA925: { setsp(sp + 12); return 1; }                                          /* DragWindow (TODO) */
    case 0xA92B: { setsp(sp + 12); wr32(sp + 12, 0); return 1; }                        /* GrowWindow */
    case 0xA904: case 0xA909: { setsp(sp + 4); return 1; }                              /* DrawGrowIcon, CalcVis */
    case 0xA90A: case 0xA90C: case 0xA90D: case 0xA83A: { setsp(sp + (n == 0xA83A ? 8 : 8)); return 1; }
    case 0xAA41: { setsp(sp + 8); return 1; }                                           /* SetWinColor */
    case 0xAA42: { setsp(sp + 8); wr_bool(sp + 8, 0); return 1; }                       /* GetAuxWin */
    case 0xA94E: {      /* PinRect(theRect, thePt): long */
        int v = (int16_t)rd16(sp), h = (int16_t)rd16(sp + 2); Rc r = rc_get(rd32(sp + 4)); setsp(sp + 8); if (h < r.l) h = r.l; if (h > r.r - 1) h = r.r - 1; if (v < r.t) v = r.t; if (v > r.b - 1) v = r.b - 1; wr16(sp + 8, v); wr16(sp + 10, h); return 1; }
    /* ---------------- Menu Manager ---------------- */
    case 0xA930: { memset(&msel, 0, sizeof msel); nmenus = 0; nhmenus = 0; wr16(0xBAA, 20); return 1; }               /* InitMenus */
    case 0xA931: { uint32_t t = rd32(sp); int id = (int16_t)rd16(sp + 4); setsp(sp + 6); wr32(sp + 6, new_menu(id, t)); return 1; }
    case 0xA932: { uint32_t m = rd32(sp); setsp(sp + 4); for (int i = 0; i < nmenus; i++) if (menus[i] == m) { memmove(&menus[i], &menus[i + 1], (nmenus - i - 1) * 4); nmenus--; break; } dispose_handle(m); return 1; }
    case 0xA9BF: {      /* GetMenu(resID) */
        int id = (int16_t)rd16(sp); setsp(sp + 2); Res *r = find_res(0x4D454E55, id); uint32_t mh = 0;
        if (r) { mh = new_handle(r->len + 8); memcpy(ram + rd32(mh), r->buf + r->off, r->len); wr32(rd32(mh) + 6, 0); } wr32(sp + 2, mh); return 1; }
    case 0xA933: { uint32_t s = rd32(sp), mh = rd32(sp + 4); setsp(sp + 8); if (mh && s) { int l = rd8(s); uint8_t b[256]; for (int i = 0; i < l; i++) b[i] = rd8(s + 1 + i); menu_parse_append(mh, b, l); } return 1; }
    case 0xA94D: { setsp(sp + 8); return 1; }                                            /* AddResMenu */
    case 0xA951: { setsp(sp + 10); return 1; }                                           /* InsertResMenu */
    case 0xA935: {      /* InsertMenu(menu, beforeID) */
        int before = (int16_t)rd16(sp); uint32_t mh = rd32(sp + 2); setsp(sp + 6);
        if (before == -1) { if (nhmenus < 40) hmenus[nhmenus++] = mh; return 1; }
        int pos = nmenus; if (before > 0) for (int i = 0; i < nmenus; i++) if (menu_id(menus[i]) == before) { pos = i; break; }
        if (nmenus < 40) { memmove(&menus[pos + 1], &menus[pos], (nmenus - pos) * 4); menus[pos] = mh; nmenus++; } return 1; }
    case 0xA936: { int id = (int16_t)rd16(sp); setsp(sp + 2); for (int i = 0; i < nmenus; i++) if (menu_id(menus[i]) == id) { memmove(&menus[i], &menus[i + 1], (nmenus - i - 1) * 4); nmenus--; break; } return 1; }
    case 0xA934: { nmenus = 0; return 1; }                                               /* ClearMenuBar */
    case 0xA937: { menubar_draw(); gfx_dirty = 1; return 1; }                           /* DrawMenuBar */
    case 0xA938: { int id = (int16_t)rd16(sp); setsp(sp + 2); hilite_menu_id = id; menubar_draw(); gfx_dirty = 1; return 1; }
    case 0xA94C: { setsp(sp + 2); return 1; }
    case 0xA949: { int id = (int16_t)rd16(sp); setsp(sp + 2); uint32_t mh = 0; for (int i = 0; i < nmenus; i++) if (menu_id(menus[i]) == id) mh = menus[i]; for (int i = 0; i < nhmenus; i++) if (menu_id(hmenus[i]) == id) mh = hmenus[i]; wr32(sp + 2, mh); return 1; }
    case 0xA950: { uint32_t mh = rd32(sp); setsp(sp + 4); wr16(sp + 4, mh ? menu_count(mh) : 0); return 1; }
    case 0xA939: case 0xA93A: { int item = (int16_t)rd16(sp); uint32_t mh = rd32(sp + 2); setsp(sp + 6); if (getenv("MENUEN")) fprintf(stderr, "[menuen] %s mh=%x item=%d ret=%06x caller=%06x\n", n == 0xA939 ? "enable" : "disable", mh, item, ret, rd32(R(M68K_REG_A6) + 4)); if (mh) menu_set_enabled(mh, item, n == 0xA939); if (item == 0) { menubar_draw(); gfx_dirty = 1; } return 1; }
    case 0xA945: { int chk = rd16(sp) != 0; int item = (int16_t)rd16(sp + 2); uint32_t mh = rd32(sp + 4); setsp(sp + 8); uint32_t p = mh ? menu_item_addr(mh, item) : 0; if (p) wr8(p + 1 + rd8(p) + 2, chk ? 0x12 : 0); return 1; }
    case 0xA944: { int mk = rd16(sp) & 0xFF; int item = (int16_t)rd16(sp + 2); uint32_t mh = rd32(sp + 4); setsp(sp + 8); uint32_t p = mh ? menu_item_addr(mh, item) : 0; if (p) wr8(p + 1 + rd8(p) + 2, mk); return 1; }
    case 0xA943: { uint32_t cp = rd32(sp); int item = (int16_t)rd16(sp + 4); uint32_t mh = rd32(sp + 6); setsp(sp + 10); uint32_t p = mh ? menu_item_addr(mh, item) : 0; wr16(cp, p ? rd8(p + 1 + rd8(p) + 2) : 0); return 1; }
    case 0xA942: { int st = rd16(sp) & 0xFF; int item = (int16_t)rd16(sp + 2); uint32_t mh = rd32(sp + 4); setsp(sp + 8); uint32_t p = mh ? menu_item_addr(mh, item) : 0; if (p) wr8(p + 1 + rd8(p) + 3, st); return 1; }
    case 0xA941: { uint32_t cp = rd32(sp); int item = (int16_t)rd16(sp + 4); uint32_t mh = rd32(sp + 6); setsp(sp + 10); uint32_t p = mh ? menu_item_addr(mh, item) : 0; wr16(cp, p ? rd8(p + 1 + rd8(p) + 3) : 0); return 1; }
    case 0xA940: { setsp(sp + 8); return 1; }
    case 0xA93F: { uint32_t cp = rd32(sp); setsp(sp + 10); wr16(cp, 0); return 1; }
    case 0xA946: { uint32_t s = rd32(sp); int item = (int16_t)rd16(sp + 4); uint32_t mh = rd32(sp + 6); setsp(sp + 10); uint32_t p = mh ? menu_item_addr(mh, item) : 0; int l = p ? rd8(p) : 0; wr8(s, l); for (int i = 0; i < l; i++) wr8(s + 1 + i, rd8(p + 1 + i)); return 1; }
    case 0xA947: {      /* SetItem(menu, item, str): rebuild item */
        uint32_t s = rd32(sp); int item = (int16_t)rd16(sp + 4); uint32_t mh = rd32(sp + 6); setsp(sp + 10); uint32_t p = mh ? menu_item_addr(mh, item) : 0; if (!p) return 1;
        int ol = rd8(p), nl = rd8(s); uint8_t attrs[4]; for (int i = 0; i < 4; i++) attrs[i] = rd8(p + 1 + ol + i); uint32_t base = rd32(mh), off = p - base, endoff = menu_end_addr(mh) - base + 1;
        int delta = nl - ol; if (delta > 0) { set_handle_size(mh, endoff + delta); base = rd32(mh); }
        memmove(ram + base + off + 1 + nl + 4, ram + base + off + 1 + ol + 4, endoff - (off + 1 + ol + 4)); wr8(base + off, nl); for (int i = 0; i < nl; i++) wr8(base + off + 1 + i, rd8(s + 1 + i)); for (int i = 0; i < 4; i++) wr8(base + off + 1 + nl + i, attrs[i]); return 1; }
    case 0xA948: { uint32_t mh = rd32(sp); setsp(sp + 4); int ww, hh; if (mh) menu_calc(mh, &ww, &hh); return 1; }
    case 0xA93D: { /* MenuSelect(startPt): long */
        int v = (int16_t)rd16(sp), h = (int16_t)rd16(sp + 2); uint32_t res = 0;
        if (menu_select_step(h, v, &res)) { if (getenv("MENUDBG")) fprintf(stderr, "  [menu] select -> %08x\n", res); setsp(sp + 4); wr32(sp + 4, res); } else { rewind_trap(ret); idle_hint = 1; } return 1; }
    case 0xA93E: {      /* MenuKey(ch): long */
        int ch = rd16(sp) & 0xFF; setsp(sp + 2); if (ch >= 'a' && ch <= 'z') ch -= 32; uint32_t res = 0;
        for (int i = 0; i < nmenus && !res; i++) { uint32_t mh = menus[i]; int c = menu_count(mh); for (int k = 1; k <= c; k++) { uint32_t p = menu_item_addr(mh, k); int key = rd8(p + 1 + rd8(p) + 1); if (key >= 'a' && key <= 'z') key -= 32; if (key && key == ch && menu_item_enabled(mh, k)) { res = ((uint32_t)menu_id(mh) << 16) | k; hilite_menu_id = menu_id(mh); menubar_draw(); gfx_dirty = 1; break; } } }
        wr32(sp + 2, res); return 1; }
    default: return 0;
    }
}
