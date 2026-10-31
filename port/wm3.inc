/* ---- Menu Manager + Event/Window trap dispatcher ---- */
static uint32_t menus[40]; static int nmenus = 0; static uint32_t hmenus[40]; static int nhmenus = 0; static int hilite_menu_id = 0;
static int menu_id(uint32_t mh) { return (int16_t)rd16(rd32(mh)); }
static uint32_t menu_title_addr(uint32_t mh) { return rd32(mh) + 14; }
static uint32_t menu_item_addr(uint32_t mh, int idx) {       /* 1-based; address of item Pstring or 0 */
    uint32_t p = menu_title_addr(mh); p += 1 + rd8(p);
    for (int i = 1;; i++) { if (rd8(p) == 0) return 0; if (i == idx) return p; p += 1 + rd8(p) + 4; }
}
static int menu_count(uint32_t mh) { uint32_t p = menu_title_addr(mh); p += 1 + rd8(p); int n = 0; while (rd8(p) != 0) { n++; p += 1 + rd8(p) + 4; } return n; }
static uint32_t menu_end_addr(uint32_t mh) { uint32_t p = menu_title_addr(mh); p += 1 + rd8(p); while (rd8(p) != 0) p += 1 + rd8(p) + 4; return p; }
static void menu_cstr(uint32_t pstr, char *out) { int l = rd8(pstr); for (int i = 0; i < l; i++) out[i] = rd8(pstr + 1 + i); out[l] = 0; }
static int menu_item_enabled(uint32_t mh, int item) { uint32_t f = rd32(rd32(mh) + 10); if (!(f & 1)) return 0; if (item > 31) return 1; return (f >> item) & 1; }
static void menu_set_enabled(uint32_t mh, int item, int en) { uint32_t a = rd32(mh) + 10, f = rd32(a); uint32_t bit = item > 31 ? 0 : 1u << item; if (en) f |= bit; else f &= ~bit; wr32(a, f); }
static void menu_insert_item_raw(uint32_t mh, int after, const uint8_t *txt, int tl, int icon, int key, int mark, int style) {
    int total = menu_count(mh); if (after > total) after = total; if (after < 0) after = 0;
    uint32_t base = rd32(mh); uint32_t ins = after == 0 ? menu_title_addr(mh) + 1 + rd8(menu_title_addr(mh)) : 0;
    if (after > 0) { uint32_t ip = menu_item_addr(mh, after); ins = ip + 1 + rd8(ip) + 4; }
    uint32_t off = ins - base, endoff = menu_end_addr(mh) - base + 1; uint32_t need = 1 + tl + 4;
    set_handle_size(mh, endoff + need); base = rd32(mh);
    memmove(ram + base + off + need, ram + base + off, endoff - off);
    wr8(base + off, tl); for (int i = 0; i < tl; i++) wr8(base + off + 1 + i, txt[i]); wr8(base + off + 1 + tl, icon); wr8(base + off + 2 + tl, key); wr8(base + off + 3 + tl, mark); wr8(base + off + 4 + tl, style);
    /* shift enable bits at or after the inserted index */
    uint32_t f = rd32(base + 10); int idx = after + 1; if (idx <= 31) { uint32_t hi = (f >> idx) << (idx + 1); uint32_t lo = f & ((1u << idx) - 1); f = (hi & ~(1u << idx)) | lo; f |= 1u << idx; wr32(base + 10, f); }
}
static void menu_parse_append(uint32_t mh, const uint8_t *s, int len) {
    int i = 0;
    while (i <= len) {
        uint8_t txt[256]; int tl = 0, icon = 0, key = 0, mark = 0, style = 0, disabled = 0;
        while (i < len && s[i] != ';' && s[i] != 0x0D) {
            uint8_t c = s[i];
            if (tl == 0 && c == '(' ) { disabled = 1; i++; continue; }
            if (c == '^' && i + 1 < len) { icon = s[i + 1] - '0'; i += 2; continue; }
            if (c == '!' && i + 1 < len) { mark = s[i + 1]; i += 2; continue; }
            if (c == '<' && i + 1 < len) { switch (s[i + 1]) { case 'B': style |= 1; break; case 'I': style |= 2; break; case 'U': style |= 4; break; case 'O': style |= 8; break; case 'S': style |= 16; break; } i += 2; continue; }
            if (c == '/' && i + 1 < len) { key = s[i + 1]; i += 2; continue; }
            txt[tl++] = c; i++;
        }
        if (tl > 0 || i < len) {
            int cnt = menu_count(mh); menu_insert_item_raw(mh, cnt, txt, tl, icon, key, mark, style);
            if (disabled || (tl == 1 && txt[0] == '-')) menu_set_enabled(mh, cnt + 1, 0); else menu_set_enabled(mh, cnt + 1, 1);
        }
        i++;
    }
}
static uint32_t new_menu(int id, uint32_t title_pstr) {
    int tl = title_pstr ? rd8(title_pstr) : 0; uint32_t mh = new_handle(14 + 1 + tl + 1); uint32_t a = rd32(mh);
    wr16(a, id); wr16(a + 2, 0); wr16(a + 4, 0); wr32(a + 6, 0); wr32(a + 10, 0xFFFFFFFF); wr8(a + 14, tl); for (int i = 0; i < tl; i++) wr8(a + 15 + i, rd8(title_pstr + 1 + i)); wr8(a + 15 + tl, 0);
    return mh;
}
static void menu_calc(uint32_t mh, int *w, int *h) {
    int n = menu_count(mh), mw = 40; for (int i = 1; i <= n; i++) { uint32_t p = menu_item_addr(mh, i); char t[256]; menu_cstr(p, t); int tw = text_w(t, 0, 12, 0) + 28; if (rd8(p + 1 + rd8(p) + 1)) tw += 30; if (tw > mw) mw = tw; }
    *w = mw; *h = n * 16; wr16(rd32(mh) + 2, mw); wr16(rd32(mh) + 4, n * 16);
}
typedef struct { uint32_t mh; int x0, x1; } MBT; static MBT mbt[40]; static int nmbt = 0;
static void mb_layout(void) {
    nmbt = 0; int x = 10;
    for (int i = 0; i < nmenus; i++) { char t[256]; menu_cstr(menu_title_addr(menus[i]), t); int w = (t[0] == 0x14) ? 22 : text_w(t, 0, 12, 1) + 16; mbt[nmbt].mh = menus[i]; mbt[nmbt].x0 = x; mbt[nmbt].x1 = x + w; nmbt++; x += w; }
}
static void draw_apple(DC *d, int x, int y, int inv) {            /* tiny apple glyph (13x14) */
    static const uint16_t g[14] = {0x0030, 0x0060, 0x00C0, 0x0000, 0x0CE0, 0x1FF0, 0x3FE0, 0x3FC0, 0x3FC0, 0x3FE0, 0x3FF0, 0x1FF0, 0x0FE0, 0x0660};
    uint32_t px = rgb_px(&d->s, inv ? (RGB){0xFFFF, 0xFFFF, 0xFFFF} : (RGB){0, 0, 0});
    for (int j = 0; j < 14; j++) for (int i = 0; i < 13; i++) if (g[j] & (0x1000 >> i)) { Rc r = {y + j, x + i, y + j + 1, x + i + 1}; (void)r; for (int k = 0; k < d->clip.n; k++) { Rc c = d->clip.r[k]; if (x + i >= c.l && x + i < c.r && y + j >= c.t && y + j < c.b) { pp(&d->s, x + i, y + j, px); break; } } }
}
static void invert_rect_dc(DC *d, Rc r) {
    for (int y = r.t; y < r.b; y++) for (int x = r.l; x < r.r; x++) {
        int in = 0; for (int i = 0; i < d->clip.n; i++) { Rc c = d->clip.r[i]; if (x >= c.l && x < c.r && y >= c.t && y < c.b) { in = 1; break; } }
        if (!in) continue; uint32_t px = gp(&d->s, x, y); if (d->s.depth == 8) { RGB c = px_rgb(&d->s, px); c.r = 65535 - c.r; c.g = 65535 - c.g; c.b = 65535 - c.b; pp(&d->s, x, y, rgb_px(&d->s, c)); } else pp(&d->s, x, y, px ^ depth_mask(d->s.depth)); }
}
static void menubar_draw(void) {
    mbar_height = rd16(0xBAA); if (!mbar_height || !screen_port) return;
    RL all = {0}; rl_add(&all, (Rc){0, 0, mbar_height, SCR_W}); DC d; screen_dc(&d, &all); fill_rect_dc(&d, (Rc){0, 0, mbar_height - 1, SCR_W}, 0); fill_rect_dc(&d, (Rc){mbar_height - 1, 0, mbar_height, SCR_W}, 1);
    mb_layout();
    for (int i = 0; i < nmbt; i++) {
        char t[256]; menu_cstr(menu_title_addr(mbt[i].mh), t);
        if (t[0] == 0x14) draw_apple(&d, mbt[i].x0 + 4, 3, 0); else text_at(&d, mbt[i].x0 + 8, mbar_height - 6, t, 0, 12, 1);
        if (menu_id(mbt[i].mh) == hilite_menu_id && hilite_menu_id) invert_rect_dc(&d, (Rc){0, mbt[i].x0, mbar_height - 1, mbt[i].x1});
    }
    dc_free(&d); rl_free(&all);
}
/* ---- dropdown ---- */
typedef struct { int active, sticky, midx, item; Rc area; uint8_t *saved; uint32_t sub; Rc sbox, sarea; uint8_t *ssaved; int sitem; } MSel; static MSel msel;
static void dd_item_draw(DC *d, uint32_t mh, int i, Rc box, int hil) {
    uint32_t p = menu_item_addr(mh, i); char t[256]; menu_cstr(p, t); Rc row = {box.t + (i - 1) * 16, box.l + 1, box.t + i * 16, box.r - 1}; int en = menu_item_enabled(mh, i);
    fill_rect_dc(d, row, 0);
    if (t[0] == '-' && !t[1]) { for (int x = row.l + 2; x < row.r - 2; x += 2) fill_rect_dc(d, (Rc){row.t + 8, x, row.t + 9, x + 1}, 1); return; }
    uint32_t sfg = d->fg; RGB grey = {0x8888, 0x8888, 0x8888};
    int tl = rd8(p); int mark = rd8(p + 1 + tl + 2), key = rd8(p + 1 + tl + 1), style = rd8(p + 1 + tl + 3); int hier = (key == 0x1B); if (hier) { mark = 0; key = 0; }
    DC *dd = d; uint32_t port = dd->port; uint32_t f0 = rd32(port + 80); (void)f0; (void)sfg;
    if (!en) { /* disabled: draw text in grey by temporarily switching port fg */ uint16_t sv[3] = {rd16(port + 36), rd16(port + 38), rd16(port + 40)}; wr16(port + 36, grey.r); wr16(port + 38, grey.g); wr16(port + 40, grey.b); DC d2; d2 = *d; dc_port_colors(port, &d2.s, &d2.fg, &d2.bg); text_at(&d2, row.l + 16, row.b - 4, t, 0, 12, style & 1); wr16(port + 36, sv[0]); wr16(port + 38, sv[1]); wr16(port + 40, sv[2]); }
    else text_at(d, row.l + 16, row.b - 4, t, 0, 12, style & 1);
    if (mark == 0x12 || mark == 0xA5 || mark == 0x13) { static const uint8_t ck[7] = {0x02, 0x06, 0x8C, 0xD8, 0x70, 0x20, 0x00}; for (int j = 0; j < 7; j++) for (int k = 0; k < 8; k++) if (ck[j] & (0x80 >> k)) fill_rect_dc(d, (Rc){row.t + 4 + j, row.l + 4 + k, row.t + 5 + j, row.l + 5 + k}, 1); }
    else if (mark) { char m[2] = {(char)mark, 0}; text_at(d, row.l + 4, row.b - 4, m, 0, 12, 0); }
    if (key) { static const uint8_t cm[7] = {0x6C, 0x92, 0x7C, 0x44, 0x7C, 0x92, 0x6C}; int kx = row.r - 24; for (int j = 0; j < 7; j++) for (int k = 0; k < 7; k++) if (cm[j] & (0x80 >> k)) fill_rect_dc(d, (Rc){row.t + 4 + j, kx + k, row.t + 5 + j, kx + k + 1}, 1);
        char kc[2] = {(char)key, 0}; text_at(d, kx + 9, row.b - 4, kc, 0, 12, 0); }
    if (hier) { for (int j = 0; j < 9; j++) { int w = (j < 5 ? j : 8 - j) + 1; fill_rect_dc(d, (Rc){row.t + 4 + j, row.r - 12, row.t + 5 + j, row.r - 12 + w}, 1); } }
    if (hil && en) invert_rect_dc(d, row);
}
static Rc dd_box(int midx) { int w, h; uint32_t mh = mbt[midx].mh; menu_calc(mh, &w, &h); int x = mbt[midx].x0; if (x + w > SCR_W - 2) x = SCR_W - 2 - w; return (Rc){mbar_height, x, mbar_height + h + 1, x + w + 1}; }
static void dd_open(int midx) {
    Rc b = dd_box(midx); Rc sa = {b.t, b.l, b.b + 1, b.r + 1}; msel.area = sa;
    int w = sa.r - sa.l, h = sa.b - sa.t; msel.saved = malloc(w * h);
    for (int y = 0; y < h; y++) memcpy(msel.saved + y * w, ram + FB_BASE + (sa.t + y) * SCR_W + sa.l, w);
    RL all = {0}; rl_add(&all, sa); DC d; screen_dc(&d, &all);
    fill_rect_dc(&d, (Rc){b.b, b.l + 1, b.b + 1, b.r + 1}, 1); fill_rect_dc(&d, (Rc){b.t + 1, b.r, b.b + 1, b.r + 1}, 1);
    fill_rect_dc(&d, b, 0); frame_rect_dc(&d, b, 1);
    uint32_t mh = mbt[midx].mh; int n = menu_count(mh); for (int i = 1; i <= n; i++) dd_item_draw(&d, mh, i, b, 0);
    dc_free(&d); rl_free(&all);
    hilite_menu_id = menu_id(mbt[midx].mh); menubar_draw();      /* redraw the title highlighted (an XOR toggle is not reversible on a palette screen and left stale highlights) */
}
static void dd_close(void) {
    if (!msel.saved) return; Rc sa = msel.area; int w = sa.r - sa.l, h = sa.b - sa.t;
    for (int y = 0; y < h; y++) memcpy(ram + FB_BASE + (sa.t + y) * SCR_W + sa.l, msel.saved + y * w, w);
    free(msel.saved); msel.saved = NULL;
    if (msel.midx >= 0) { hilite_menu_id = 0; menubar_draw(); }
    gfx_dirty = 1;
}
static void dd_hilite(int item, int on) {
    if (msel.midx < 0 || item < 1) return; Rc b = dd_box(msel.midx); Rc row = {b.t + (item - 1) * 16, b.l + 1, b.t + item * 16, b.r - 1};
    uint32_t mh = mbt[msel.midx].mh; if (!menu_item_enabled(mh, item)) return;
    RL all = {0}; rl_add(&all, row); DC d; screen_dc(&d, &all); invert_rect_dc(&d, row); dc_free(&d); rl_free(&all); (void)on;
}
static int mbar_title_at(int x) { for (int i = 0; i < nmbt; i++) if (x >= mbt[i].x0 && x < mbt[i].x1) return i; return -1; }
static uint32_t menu_by_id(int id) { for (int i = 0; i < nmenus; i++) if (menu_id(menus[i]) == id) return menus[i]; for (int i = 0; i < nhmenus; i++) if (menu_id(hmenus[i]) == id) return hmenus[i]; return 0; }
static void box_row_invert(Rc box, int item) { Rc row = {box.t + (item - 1) * 16, box.l + 1, box.t + item * 16, box.r - 1}; RL all = {0}; rl_add(&all, row); DC d; screen_dc(&d, &all); invert_rect_dc(&d, row); dc_free(&d); rl_free(&all); }
static void sub_close(void) {
    if (!msel.ssaved) return; Rc sa = msel.sarea; int w = sa.r - sa.l, h = sa.b - sa.t;
    for (int y = 0; y < h; y++) memcpy(ram + FB_BASE + (sa.t + y) * SCR_W + sa.l, msel.ssaved + y * w, w);
    free(msel.ssaved); msel.ssaved = NULL; msel.sub = 0; msel.sitem = 0; gfx_dirty = 1;
}
static void sub_open(uint32_t mh, Rc parent_row) {
    int w, h; menu_calc(mh, &w, &h); int x = parent_row.r - 2, y = parent_row.t - 1; if (x + w + 2 > SCR_W) x = parent_row.l - w + 2; if (y + h + 2 > SCR_H) y = SCR_H - h - 2;
    msel.sub = mh; msel.sbox = (Rc){y, x, y + h + 1, x + w + 1}; msel.sarea = (Rc){msel.sbox.t, msel.sbox.l, msel.sbox.b + 1, msel.sbox.r + 1}; msel.sitem = 0;
    Rc sa = msel.sarea; int sw = sa.r - sa.l, sh = sa.b - sa.t; msel.ssaved = malloc(sw * sh);
    for (int yy = 0; yy < sh; yy++) memcpy(msel.ssaved + yy * sw, ram + FB_BASE + (sa.t + yy) * SCR_W + sa.l, sw);
    RL all = {0}; rl_add(&all, sa); DC d; screen_dc(&d, &all);
    fill_rect_dc(&d, (Rc){msel.sbox.b, msel.sbox.l + 1, msel.sbox.b + 1, msel.sbox.r + 1}, 1); fill_rect_dc(&d, (Rc){msel.sbox.t + 1, msel.sbox.r, msel.sbox.b + 1, msel.sbox.r + 1}, 1);
    fill_rect_dc(&d, msel.sbox, 0); frame_rect_dc(&d, msel.sbox, 1); int n = menu_count(mh); for (int i = 1; i <= n; i++) dd_item_draw(&d, mh, i, msel.sbox, 0);
    dc_free(&d); rl_free(&all);
}
static uint32_t hier_sub_of(uint32_t mh, int item) { if (item < 1) return 0; uint32_t p = menu_item_addr(mh, item); if (!p) return 0; int tl = rd8(p); if (rd8(p + 1 + tl + 1) != 0x1B) return 0; return menu_by_id(rd8(p + 1 + tl + 2)); }
static int menu_select_step(int startx, int starty, uint32_t *result) {      /* returns 1 when finished (result set) */
    if (!msel.active) {
        mb_layout(); ev_pump(); for (int i = 0; i < evq_n; i++) if (evq[(evq_h + i) & 255].what == 2) { for (int j = i; j + 1 < evq_n; j++) evq[(evq_h + j) & 255] = evq[(evq_h + j + 1) & 255]; evq_n--; break; }
        memset(&msel, 0, sizeof msel); msel.active = 1; msel.midx = -1; swallow_up = plat_btn ? 1 : 0;
        int t = mbar_title_at(startx); if (t >= 0) { msel.midx = t; dd_open(t); } (void)starty;
    }
    ev_pump();
    int mx = plat_mx, my = plat_my, btn = plat_btn;
    if (!btn && msel.sticky && !msel.item) { /* waiting for a click while sticky */ }
    if (btn || msel.sticky) {
        int t = (my < mbar_height) ? mbar_title_at(mx) : -1;
        if (t >= 0 && t != msel.midx && btn) { if (msel.midx >= 0) dd_close(); msel.midx = t; msel.item = 0; dd_open(t); }
        else if (msel.midx >= 0) {
            Rc b = dd_box(msel.midx); int it = 0; uint32_t mh = mbt[msel.midx].mh;
            int in_sub = msel.sub && mx >= msel.sbox.l && mx < msel.sbox.r - 1 && my >= msel.sbox.t && my < msel.sbox.b - 1;
            if (in_sub) { int si = (my - msel.sbox.t) / 16 + 1; if (!menu_item_enabled(msel.sub, si)) si = 0; if (si != msel.sitem) { if (msel.sitem) box_row_invert(msel.sbox, msel.sitem); msel.sitem = si; if (si) box_row_invert(msel.sbox, si); } }
            else {
                if (mx >= b.l && mx < b.r - 1 && my >= b.t && my < b.b - 1) it = (my - b.t) / 16 + 1;
                if (msel.sub && it == msel.item) { if (msel.sitem) { box_row_invert(msel.sbox, msel.sitem); msel.sitem = 0; } }
                else if (it != msel.item) {
                    if (msel.sub) sub_close();
                    dd_hilite(msel.item, 0); msel.item = it; dd_hilite(it, 1);
                    uint32_t sm = menu_item_enabled(mh, it) ? hier_sub_of(mh, it) : 0; if (sm) sub_open(sm, (Rc){b.t + (it - 1) * 16, b.l + 1, b.t + it * 16, b.r - 1});
                }
            }
        }
    }
    if (btn) { if (msel.sticky) { /* second press: choose or cancel */ if (msel.item && menu_item_enabled(mbt[msel.midx].mh, msel.item)) { swallow_up = 1; goto finish; } else if (msel.item == 0 && (my >= mbar_height || mbar_title_at(mx) < 0)) { swallow_up = 1; msel.item = 0; goto finish; } } return 0; }
    /* button up */
    if (!msel.sticky && msel.item == 0 && msel.midx >= 0 && my < mbar_height) { msel.sticky = 1; return 0; }       /* released on title: keep menu open */
    if (msel.sticky) return 0;
finish: {
        int mid = 0, item = 0, topid = 0;
        if (msel.midx >= 0 && msel.item && menu_item_enabled(mbt[msel.midx].mh, msel.item)) { mid = menu_id(mbt[msel.midx].mh); item = msel.item; topid = mid; if (msel.sub) { if (msel.sitem) { mid = menu_id(msel.sub); item = msel.sitem; } else { mid = 0; item = 0; topid = 0; } } }
        sub_close();
        int midx = msel.midx; dd_close(); msel.midx = -1; (void)midx;
        if (mid) { hilite_menu_id = topid; menubar_draw(); gfx_dirty = 1; }
        *result = ((uint32_t)mid << 16) | (uint32_t)item; msel.active = 0; swallow_up = 0; return 1; }
}
