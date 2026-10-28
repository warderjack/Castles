/* ---- Standard File "get" dialog (SFGetFile): overlay modal drawn on the screen, folder list with scrolling ---- */
#define SFG_MAX 256
static struct {
    int active; Rc box, sa; uint8_t *saved; int n, top, sel, def_open; char dir[1100]; char name[SFG_MAX][64]; uint8_t isdir[SFG_MAX];
    uint32_t last_tick; int last_item; int result; char rootdir[1100]; int btn; int track;
} sfg;
#define SFG_ROWS 7
static int sfg_cmp(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }
static void sfg_scan(void) {
    sfg.n = 0; sfg.top = 0; sfg.sel = -1; DIR *dp = opendir(sfg.dir); if (!dp) return; struct dirent *e; static char tmp[SFG_MAX][64]; static uint8_t td[SFG_MAX]; int nn = 0;
    while ((e = readdir(dp)) && nn < SFG_MAX) { if (e->d_name[0] == '.') continue; char nm[96]; strncpy(nm, e->d_name, 95); nm[95] = 0; size_t L = strlen(nm); char full[1300]; struct stat st;
        snprintf(full, sizeof full, "%s/%s", sfg.dir, nm); if (stat(full, &st) != 0) continue; int dir = S_ISDIR(st.st_mode);
        if (!dir && L > 4 && !strcasecmp(nm + L - 4, ".bin")) nm[L - 4] = 0;
        int dup = 0; for (int i = 0; i < nn; i++) if (!strcmp(tmp[i], nm)) dup = 1; if (dup) continue;
        strncpy(tmp[nn], nm, 63); tmp[nn][63] = 0; td[nn] = dir; nn++; }
    closedir(dp);
    for (int i = 0; i < nn - 1; i++) for (int j = i + 1; j < nn; j++) if (strcasecmp(tmp[i], tmp[j]) > 0) { char t[64]; strcpy(t, tmp[i]); strcpy(tmp[i], tmp[j]); strcpy(tmp[j], t); uint8_t x = td[i]; td[i] = td[j]; td[j] = x; }
    for (int i = 0; i < nn; i++) { strcpy(sfg.name[i], tmp[i]); sfg.isdir[i] = td[i]; } sfg.n = nn;
}
static Rc sfg_list_rc(void) { return (Rc){sfg.box.t + 34, sfg.box.l + 14, sfg.box.t + 34 + SFG_ROWS * 16, sfg.box.l + 210}; }
static Rc sfg_btn_rc(int k) { static const int ys[4] = {34, 62, 98, 126}; return (Rc){sfg.box.t + ys[k], sfg.box.l + 244, sfg.box.t + ys[k] + 22, sfg.box.l + 324}; }
static void sfg_ctext(DC *d, Rc r, const char *s, int face) { int w = text_w(s, 0, 12, face); text_at(d, (r.l + r.r - w) / 2, r.b - 6, s, 0, 12, face); }
static void sfg_draw(void) {
    RL all = {0}; rl_add(&all, sfg.sa); DC d; screen_dc(&d, &all); Rc b = sfg.box;
    fill_rect_dc(&d, (Rc){b.b, b.l + 3, b.b + 2, b.r + 2}, 1); fill_rect_dc(&d, (Rc){b.t + 3, b.r, b.b + 2, b.r + 2}, 1);
    fill_rect_dc(&d, b, 0); frame_rect_dc(&d, b, 1); frame_rect_dc(&d, (Rc){b.t + 2, b.l + 2, b.b - 2, b.r - 2}, 1);
    const char *dn = strrchr(sfg.dir, '/'); dn = dn ? dn + 1 : sfg.dir; Rc tr = {b.t + 6, b.l + 14, b.t + 28, b.l + 226}; frame_rect_dc(&d, tr, 1); fill_rect_dc(&d, (Rc){tr.b, tr.l + 2, tr.b + 1, tr.r + 1}, 1); fill_rect_dc(&d, (Rc){tr.t + 2, tr.r, tr.b + 1, tr.r + 1}, 1);
    fill_rect_dc(&d, (Rc){tr.t + 7, tr.l + 5, tr.t + 8, tr.l + 15}, 1); fill_rect_dc(&d, (Rc){tr.t + 8, tr.l + 5, tr.t + 16, tr.l + 6}, 1); fill_rect_dc(&d, (Rc){tr.t + 15, tr.l + 5, tr.t + 16, tr.l + 17}, 1); fill_rect_dc(&d, (Rc){tr.t + 8, tr.l + 16, tr.t + 16, tr.l + 17}, 1);
    text_at(&d, tr.l + 24, tr.b - 6, dn, 0, 12, 0);
    Rc lr = sfg_list_rc(); fill_rect_dc(&d, lr, 0); frame_rect_dc(&d, lr, 1);
    for (int i = 0; i < SFG_ROWS; i++) { int k = sfg.top + i; if (k >= sfg.n) break; Rc row = {lr.t + i * 16, lr.l + 1, lr.t + i * 16 + 16, lr.r - 1};
        char nm[70]; snprintf(nm, sizeof nm, "%s", sfg.name[k]); text_at(&d, row.l + 4, row.b - 4, nm, 0, 12, 0);
        if (sfg.isdir[k]) { for (int j = 0; j < 9; j++) { int w = (j < 5 ? j : 8 - j) + 1; fill_rect_dc(&d, (Rc){row.t + 4 + j, row.r - 14, row.t + 5 + j, row.r - 14 + w}, 1); } }
        if (k == sfg.sel) invert_rect_dc(&d, row); }
    Rc sb = {lr.t, lr.r, lr.b, lr.r + 16}; fill_rect_dc(&d, sb, 0); frame_rect_dc(&d, sb, 1);
    Rc up = {sb.t, sb.l, sb.t + 16, sb.r}, dn2 = {sb.b - 16, sb.l, sb.b, sb.r}; frame_rect_dc(&d, up, 1); frame_rect_dc(&d, dn2, 1);
    for (int j = 0; j < 4; j++) { fill_rect_dc(&d, (Rc){up.t + 5 + j, up.l + 8 - j, up.t + 6 + j, up.l + 8 + j + 1}, 1); fill_rect_dc(&d, (Rc){dn2.b - 6 - j, dn2.l + 8 - j, dn2.b - 5 - j, dn2.l + 8 + j + 1}, 1); }
    if (sfg.n > SFG_ROWS) { int track = sb.b - sb.t - 32; int th = track * SFG_ROWS / sfg.n; if (th < 16) th = 16; int maxtop = sfg.n - SFG_ROWS; int ty = sb.t + 16 + (track - th) * sfg.top / maxtop; fill_rect_dc(&d, (Rc){ty, sb.l + 1, ty + th, sb.r - 1}, 1); fill_rect_dc(&d, (Rc){ty + 1, sb.l + 2, ty + th - 1, sb.r - 2}, 0); }
    else { RL none = {0}; (void)none; }
    static const char *lab[4] = {"Eject", "Desktop", "Open", "Cancel"};
    for (int k = 0; k < 4; k++) { Rc r = sfg_btn_rc(k); int en = !(k < 2) && !(k == 2 && sfg.sel < 0);
        if (k == 2) { Rc ring = {r.t - 4, r.l - 4, r.b + 4, r.r + 4}; if (en) { frame_rect_dc(&d, ring, 1); frame_rect_dc(&d, (Rc){ring.t + 1, ring.l + 1, ring.b - 1, ring.r - 1}, 1); frame_rect_dc(&d, (Rc){ring.t + 2, ring.l + 2, ring.b - 2, ring.r - 2}, 1); fill_rect_dc(&d, (Rc){r.t, r.l, r.b, r.r}, 0); } }
        frame_rect_dc(&d, r, 1); sfg_ctext(&d, r, lab[k], 0);
        if (!en) { /* dim: overdraw with 50% checker */ for (int y = r.t + 1; y < r.b - 1; y++) for (int x = r.l + 1 + (y & 1); x < r.r - 1; x += 2) fill_rect_dc(&d, (Rc){y, x, y + 1, x + 1}, 0); }
        if (sfg.btn == k + 1 && en) invert_rect_dc(&d, r); }
    dc_free(&d); rl_free(&all);
}
static void sfg_enter(const char *nm) { char nd[1100]; snprintf(nd, sizeof nd, "%s/%s", sfg.dir, nm); strncpy(sfg.dir, nd, 1099); sfg_scan(); }
static int sfg_step(uint32_t ret, uint32_t reply, int vv, int hh) {
    if (!sfg.active) {
        memset(&sfg, 0, sizeof sfg); sfg.active = 1; const char *base = dir_path(cur_dir); strncpy(sfg.dir, base ? base : ".", 1099); strncpy(sfg.rootdir, sfg.dir, 1099); sfg_scan();
        int T = vv, L = hh; if (T < mbar_height + 4 || T > SCR_H - 200) T = (SCR_H - 190) / 2 + 10; if (L < 0 || L > SCR_W - 350) L = (SCR_W - 340) / 2;
        sfg.box = (Rc){T, L, T + 170, L + 340}; sfg.sa = (Rc){sfg.box.t, sfg.box.l, sfg.box.b + 3, sfg.box.r + 3}; if (sfg.sa.b > SCR_H) sfg.sa.b = SCR_H;
        int sw = sfg.sa.r - sfg.sa.l, sh = sfg.sa.b - sfg.sa.t; sfg.saved = malloc(sw * sh);
        for (int y = 0; y < sh; y++) memcpy(sfg.saved + y * sw, ram + FB_BASE + (sfg.sa.t + y) * SCR_W + sfg.sa.l, sw);
        sfg.last_item = -1; sfg_draw(); swallow_up = 0;
    }
    static uint32_t evp; if (!evp) evp = alloc_block(24);
    int done = 0, openit = 0; ev_pump(); { static int cnt; if (getenv("SFGDBG") && (++cnt % 200 == 1 || plat_btn)) fprintf(stderr, "  [sfg] step %d evq=%d btn=%d mx=%d my=%d\n", cnt, evq_n, plat_btn, plat_mx, plat_my); }
    if (sfg.track == 1) {      /* tracking a button press */
        Rc r = sfg_btn_rc(sfg.btn - 1); int inside = plat_mx >= r.l && plat_mx < r.r && plat_my >= r.t && plat_my < r.b;
        if (plat_btn) { if (!inside) { if (sfg.btn > 0) { sfg.btn = -sfg.btn; sfg_draw(); } } else if (sfg.btn < 0) { sfg.btn = -sfg.btn; sfg_draw(); } idle_hint = 1; rewind_trap(ret); return 0; }
        int k = sfg.btn; sfg.btn = 0; sfg.track = 0; if (k > 0) { if (k == 4) { sfg.result = 0; done = 1; } else if (k == 3) openit = 1; } sfg_draw();
    } else if (get_event(evp, (1 << 1) | (1 << 3) | (1 << 5), 1)) {
        int what = rd16(evp); if (getenv("SFGDBG")) fprintf(stderr, "  [sfg] event %d v=%d h=%d\n", what, (int16_t)rd16(evp + 10), (int16_t)rd16(evp + 12));
        if (what == 1) {
            int gv = (int16_t)rd16(evp + 10), gh = (int16_t)rd16(evp + 12); Rc lr = sfg_list_rc();
            if (gh >= lr.l && gh < lr.r && gv >= lr.t && gv < lr.b) { int k = sfg.top + (gv - lr.t) / 16; if (k < sfg.n) { uint32_t t = ticks_now(); int dbl = (k == sfg.last_item && t - sfg.last_tick < 25); sfg.sel = k; sfg.last_item = k; sfg.last_tick = t; sfg_draw(); if (dbl) openit = 1; } }
            else if (gh >= lr.r && gh < lr.r + 16 && gv >= lr.t && gv < lr.b) { int maxtop = sfg.n > SFG_ROWS ? sfg.n - SFG_ROWS : 0; if (gv < lr.t + 16) sfg.top--; else if (gv >= lr.b - 16) sfg.top++; else if (gv < lr.t + 16 + (lr.b - lr.t - 32) * sfg.top / (maxtop ? maxtop : 1)) sfg.top -= SFG_ROWS - 1; else sfg.top += SFG_ROWS - 1; if (sfg.top < 0) sfg.top = 0; if (sfg.top > maxtop) sfg.top = maxtop; sfg_draw(); }
            else { for (int k = 2; k < 4; k++) { Rc r = sfg_btn_rc(k); if (gh >= r.l && gh < r.r && gv >= r.t && gv < r.b && !(k == 2 && sfg.sel < 0)) { sfg.btn = k + 1; sfg.track = 1; sfg_draw(); } }
                Rc tr = {sfg.box.t + 6, sfg.box.l + 14, sfg.box.t + 28, sfg.box.l + 226}; if (gh >= tr.l && gh < tr.r && gv >= tr.t && gv < tr.b && strcmp(sfg.dir, sfg.rootdir)) { char *sl = strrchr(sfg.dir, '/'); if (sl) { *sl = 0; sfg_scan(); sfg_draw(); } } }
        } else { int ch = rd32(evp + 2) & 0xFF, mods = rd16(evp + 14);
            if (ch == 13 || ch == 3) { if (sfg.sel >= 0) openit = 1; }
            else if (ch == 27 || (ch == '.' && (mods & 0x100))) { sfg.result = 0; done = 1; }
            else if (ch == 0x1F || ch == 0x1E) { int k = sfg.sel < 0 ? 0 : sfg.sel + (ch == 0x1F ? 1 : -1); if (k >= 0 && k < sfg.n) { sfg.sel = k; if (k < sfg.top) sfg.top = k; if (k >= sfg.top + SFG_ROWS) sfg.top = k - SFG_ROWS + 1; sfg_draw(); } }
            else if (ch >= 32 && ch < 127) { for (int k = 0; k < sfg.n; k++) if (toupper((unsigned char)sfg.name[k][0]) >= toupper(ch)) { sfg.sel = k; if (k < sfg.top || k >= sfg.top + SFG_ROWS) sfg.top = k < sfg.n - SFG_ROWS ? k : (sfg.n > SFG_ROWS ? sfg.n - SFG_ROWS : 0); sfg_draw(); break; } } }
    }
    if (openit && sfg.sel >= 0) { if (sfg.isdir[sfg.sel]) { char nm[64]; strcpy(nm, sfg.name[sfg.sel]); sfg_enter(nm); sfg_draw(); }
        else { sfg.result = 1; done = 1; } }
    if (!done) { idle_hint = 1; rewind_trap(ret); return 0; }
    int sw = sfg.sa.r - sfg.sa.l, sh = sfg.sa.b - sfg.sa.t; for (int y = 0; y < sh; y++) memcpy(ram + FB_BASE + (sfg.sa.t + y) * SCR_W + sfg.sa.l, sfg.saved + y * sw, sw);
    free(sfg.saved); sfg.saved = NULL; gfx_dirty = 1; sfg.active = 0;
    wr8(reply, sfg.result); wr8(reply + 1, 0); wr32(reply + 2, 0); wr16(reply + 8, 1);
    if (sfg.result) { wr16(reply + 6, 0x100 + dir_id_for(sfg.dir)); const char *nm = sfg.name[sfg.sel]; int L = strlen(nm); if (L > 31) L = 31; wr8(reply + 10, L); for (int i = 0; i < L; i++) wr8(reply + 11 + i, nm[i]); }
    return 1;
}
