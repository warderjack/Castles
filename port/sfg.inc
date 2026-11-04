/* ---- Standard File "get" dialog (SFGetFile): overlay modal drawn on the screen, folder list with scrolling ---- */
#define SFG_MAX 256
static struct {
    int active; Rc box, sa; uint8_t *saved; int n, top, sel, def_open; char dir[1100]; char name[SFG_MAX][64]; uint8_t isdir[SFG_MAX];
    uint32_t last_tick; int last_item; int result; char rootdir[1100]; int btn; int track; int dragoff; int put; char nin[64]; char prompt[80];
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

/* System 7 look: rounded-rect buttons, folder icons, scroll bar with arrow boxes, gray track and a gripped thumb */
static int rr_in(int R, int y) { for (int x = 0; x < R; x++) { int a = 2 * (R - x) - 1, b = 2 * (R - y) - 1; if (a * a + b * b <= 4 * R * R) return x; } return R; }
static void sfg_hl(DC *d, int y, int x0, int x1, int c) { if (x1 > x0) fill_rect_dc(d, (Rc){y, x0, y + 1, x1}, c); }
static void sfg_rrect(DC *d, Rc r, int R, int c) {
    int w = r.r - r.l, h = r.b - r.t; if (R * 2 > h) R = h / 2; if (R * 2 > w) R = w / 2;
    for (int y = 0; y < R; y++) { int i = rr_in(R, y); if (!y) { sfg_hl(d, r.t, r.l + i, r.r - i, c); sfg_hl(d, r.b - 1, r.l + i, r.r - i, c); }
        else { int pv = rr_in(R, y - 1), e = pv > i ? pv : i + 1; sfg_hl(d, r.t + y, r.l + i, r.l + e, c); sfg_hl(d, r.t + y, r.r - e, r.r - i, c); sfg_hl(d, r.b - 1 - y, r.l + i, r.l + e, c); sfg_hl(d, r.b - 1 - y, r.r - e, r.r - i, c); } }
    if (h - 2 * R > 0) { fill_rect_dc(d, (Rc){r.t + R, r.l, r.b - R, r.l + 1}, c); fill_rect_dc(d, (Rc){r.t + R, r.r - 1, r.b - R, r.r}, c); }
}
/* invert (mode 0) or dim with a white checker (mode 1) the inside of a rounded rect */
static void sfg_rarea(DC *d, Rc r, int R, int mode) {
    int h = r.b - r.t; if (R * 2 > h) R = h / 2;
    for (int y = r.t; y < r.b; y++) { int k = y - r.t, kk = k < R ? k : (r.b - 1 - y < R ? r.b - 1 - y : R), i = kk < R ? rr_in(R, kk) : 0; int x0 = r.l + i, x1 = r.r - i;
        if (mode == 0) invert_rect_dc(d, (Rc){y, x0, y + 1, x1}); else for (int x = x0 + (y & 1); x < x1; x += 2) fill_rect_dc(d, (Rc){y, x, y + 1, x + 1}, 0); }
}
static void sfg_folder(DC *d, int x, int y) {      /* 14x11 folder icon */
    sfg_hl(d, y, x, x + 6, 1); fill_rect_dc(d, (Rc){y, x, y + 11, x + 1}, 1); fill_rect_dc(d, (Rc){y + 2, x + 13, y + 11, x + 14}, 1);
    sfg_hl(d, y + 2, x + 6, x + 13, 1); fill_rect_dc(d, (Rc){y, x + 5, y + 3, x + 6}, 1); sfg_hl(d, y + 10, x, x + 14, 1);
    for (int k = 3; k < 10; k++) for (int xx = x + 1 + (k & 1); xx < x + 13; xx += 2) fill_rect_dc(d, (Rc){y + k, xx, y + k + 1, xx + 1}, 1); }
static void sfg_tri(DC *d, int cx, int ty, int up, int c) { for (int j = 0; j < 5; j++) { int hw = up ? j : 4 - j; sfg_hl(d, ty + j, cx - hw, cx + hw + 1, c); } }
static void sfg_thumb(int *ty, int *th) { Rc lr = sfg_list_rc(); int track = (lr.b - lr.t) - 32; int t = track * SFG_ROWS / (sfg.n ? sfg.n : 1); if (t < 16) t = 16; if (t > track) t = track; int maxtop = sfg.n > SFG_ROWS ? sfg.n - SFG_ROWS : 1; *th = t; *ty = lr.t + 16 + (track - t) * sfg.top / maxtop; }
static void sfg_draw(void) {
    RL all = {0}; rl_add(&all, sfg.sa); DC d; screen_dc(&d, &all); Rc b = sfg.box;
    fill_rect_dc(&d, b, 0); frame_rect_dc(&d, b, 1);      /* System 7 color dialog frame, same as dBoxProc windows */
    { static const int gv[3] = {0xD8, 0xB8, 0x90};
      for (int k = 0; k < 3; k++) { Rc q = {b.t + 1 + k, b.l + 1 + k, b.b - 1 - k, b.r - 1 - k}; int lt = gv[k], dk = gv[2 - k];
          fill_rgb_dc(&d, (Rc){q.t, q.l, q.t + 1, q.r}, lt, lt + 8); fill_rgb_dc(&d, (Rc){q.t, q.l, q.b, q.l + 1}, lt, lt + 8);
          fill_rgb_dc(&d, (Rc){q.b - 1, q.l, q.b, q.r}, dk, dk + 8); fill_rgb_dc(&d, (Rc){q.t, q.r - 1, q.b, q.r}, dk, dk + 8); }
      frame_rect_dc(&d, (Rc){b.t + 4, b.l + 4, b.b - 4, b.r - 4}, 1); }
    const char *dn = strrchr(sfg.dir, '/'); dn = dn ? dn + 1 : sfg.dir; Rc tr = {b.t + 6, b.l + 14, b.t + 28, b.l + 226}; frame_rect_dc(&d, tr, 1); fill_rect_dc(&d, (Rc){tr.b, tr.l + 2, tr.b + 1, tr.r + 1}, 1); fill_rect_dc(&d, (Rc){tr.t + 2, tr.r, tr.b + 1, tr.r + 1}, 1);
    sfg_folder(&d, tr.l + 6, tr.t + 5); sfg_tri(&d, tr.r - 14, tr.t + 9, 0, 1);
    text_at(&d, tr.l + 24, tr.b - 6, dn, 0, 12, 0);
    Rc lr = sfg_list_rc(); fill_rect_dc(&d, lr, 0); frame_rect_dc(&d, lr, 1);
    for (int i = 0; i < SFG_ROWS; i++) { int k = sfg.top + i; if (k >= sfg.n) break; Rc row = {lr.t + i * 16, lr.l + 1, lr.t + i * 16 + 16, lr.r - 1};
        char nm[70]; snprintf(nm, sizeof nm, "%s", sfg.name[k]); text_at(&d, row.l + (sfg.isdir[k] ? 24 : 6), row.b - 4, nm, 0, 12, 0);
        if (sfg.put && !sfg.isdir[k]) { for (int y = row.t; y < row.b; y++) for (int x = row.l + (y & 1); x < row.r; x += 2) fill_rect_dc(&d, (Rc){y, x, y + 1, x + 1}, 0); }
        if (sfg.isdir[k]) sfg_folder(&d, row.l + 5, row.t + 2);
        if (k == sfg.sel) invert_rect_dc(&d, row); }
    { Rc sb = {lr.t, lr.r, lr.b, lr.r + 16}; int act = sfg.n > SFG_ROWS; fill_rect_dc(&d, sb, 0); frame_rect_dc(&d, sb, 1);
      Rc up = {sb.t, sb.l, sb.t + 16, sb.r}, dn2 = {sb.b - 16, sb.l, sb.b, sb.r}; frame_rect_dc(&d, up, 1); frame_rect_dc(&d, dn2, 1);
      if (act) { for (int y = sb.t + 17; y < sb.b - 17; y++) for (int x = sb.l + 1 + (y & 1); x < sb.r - 1; x += 2) fill_rect_dc(&d, (Rc){y, x, y + 1, x + 1}, 1);
          sfg_tri(&d, sb.l + 8, up.t + 5, 1, 1); sfg_tri(&d, sb.l + 8, dn2.t + 6, 0, 1);
          int ty, th; sfg_thumb(&ty, &th); Rc tb = {ty, sb.l, ty + th, sb.r}; fill_rect_dc(&d, tb, 0); frame_rect_dc(&d, tb, 1);
          for (int g = -2; g <= 2; g += 2) if (th >= 12) sfg_hl(&d, ty + th / 2 + g, sb.l + 4, sb.r - 4, 1); }
      else { sfg_tri(&d, sb.l + 8, up.t + 5, 1, 1); sfg_tri(&d, sb.l + 8, dn2.t + 6, 0, 1);      /* nothing to scroll: dimmed arrows */
          for (int y = sb.t + 1; y < sb.b - 1; y++) for (int x = sb.l + 1 + (y & 1); x < sb.r - 1; x += 2) if (y < up.b - 1 || y >= dn2.t + 1) fill_rect_dc(&d, (Rc){y, x, y + 1, x + 1}, 0); } }
    static const char *lab[4] = {"Eject", "Desktop", "Open", "Cancel"}; if (sfg.put) lab[2] = "Save";
    if (sfg.put) { Rc nf = {lr.b + 20, lr.l, lr.b + 38, lr.r + 16}; text_at(&d, nf.l, nf.t - 4, sfg.prompt[0] ? sfg.prompt : "Save as:", 0, 12, 0); fill_rect_dc(&d, nf, 0); frame_rect_dc(&d, nf, 1); text_at(&d, nf.l + 4, nf.b - 5, sfg.nin, 0, 12, 0); int cw = text_w(sfg.nin, 0, 12, 0); fill_rect_dc(&d, (Rc){nf.t + 3, nf.l + 4 + cw, nf.b - 3, nf.l + 5 + cw}, 1); }
    for (int k = 0; k < 4; k++) { Rc r = sfg_btn_rc(k); int en = !(k < 2) && !(k == 2 && (sfg.put ? !sfg.nin[0] : sfg.sel < 0));
        if (k == 2 && en) { for (int j = 0; j < 3; j++) sfg_rrect(&d, (Rc){r.t - 4 + j, r.l - 4 + j, r.b + 4 - j, r.r + 4 - j}, 12 - j, 1); }
        fill_rect_dc(&d, r, 0); sfg_rrect(&d, r, 8, 1); sfg_ctext(&d, r, lab[k], 0);
        if (!en) sfg_rarea(&d, r, 8, 1);
        if (sfg.btn == k + 1 && en) sfg_rarea(&d, r, 8, 0); }
    dc_free(&d); rl_free(&all);
}
static void sfg_enter(const char *nm) { char nd[1100]; snprintf(nd, sizeof nd, "%s/%s", sfg.dir, nm); strncpy(sfg.dir, nd, 1099); sfg_scan(); }
static int sfg_step(uint32_t ret, uint32_t reply, int vv, int hh, int put, uint32_t prompt, uint32_t orig) {
    if (!sfg.active) {
        memset(&sfg, 0, sizeof sfg); sfg.active = 1; sfg.put = put; if (put) { pstr(prompt, sfg.prompt, 80); pstr(orig, sfg.nin, 32); } const char *base = dir_path(cur_dir); strncpy(sfg.dir, base ? base : ".", 1099); strncpy(sfg.rootdir, sfg.dir, 1099); sfg_scan();
        int T = vv, L = hh; if (T < mbar_height + 4 || T > SCR_H - 200) T = (SCR_H - 190) / 2 + 10; if (L < 0 || L > SCR_W - 350) L = (SCR_W - 340) / 2;
        sfg.box = (Rc){T, L, T + (sfg.put ? 200 : 170), L + 340}; sfg.sa = (Rc){sfg.box.t, sfg.box.l, sfg.box.b + 3, sfg.box.r + 3}; if (sfg.sa.b > SCR_H) sfg.sa.b = SCR_H;
        int sw = sfg.sa.r - sfg.sa.l, sh = sfg.sa.b - sfg.sa.t; sfg.saved = malloc(sw * sh);
        for (int y = 0; y < sh; y++) memcpy(sfg.saved + y * sw, ram + FB_BASE + (sfg.sa.t + y) * SCR_W + sfg.sa.l, sw);
        sfg.last_item = -1; sfg_draw(); swallow_up = 0;
    }
    static uint32_t evp; if (!evp) evp = alloc_block(24);
    int done = 0, openit = 0; ev_pump(); { static int cnt; if (getenv("SFGDBG") && (++cnt % 200 == 1 || plat_btn)) fprintf(stderr, "  [sfg] step %d evq=%d btn=%d mx=%d my=%d\n", cnt, evq_n, plat_btn, plat_mx, plat_my); }
    if (sfg.track == 2) {      /* dragging the scroll box */
        if (plat_btn) { Rc lr = sfg_list_rc(); int ty, th; sfg_thumb(&ty, &th); int track = (lr.b - lr.t) - 32, span = track - th, maxtop = sfg.n - SFG_ROWS; int nt = span > 0 ? (plat_my - sfg.dragoff - (lr.t + 16)) * maxtop / span : 0; if (nt < 0) nt = 0; if (nt > maxtop) nt = maxtop; if (nt != sfg.top) { sfg.top = nt; sfg_draw(); } idle_hint++; rewind_trap(ret); return 0; }
        sfg.track = 0;
    } else if (sfg.track == 1) {      /* tracking a button press */
        Rc r = sfg_btn_rc(sfg.btn - 1); int inside = plat_mx >= r.l && plat_mx < r.r && plat_my >= r.t && plat_my < r.b;
        if (plat_btn) { if (!inside) { if (sfg.btn > 0) { sfg.btn = -sfg.btn; sfg_draw(); } } else if (sfg.btn < 0) { sfg.btn = -sfg.btn; sfg_draw(); } idle_hint++; rewind_trap(ret); return 0; }
        int k = sfg.btn; sfg.btn = 0; sfg.track = 0; if (k > 0) { if (k == 4) { sfg.result = 0; done = 1; } else if (k == 3) { if (sfg.put) { sfg.result = 1; done = 1; } else openit = 1; } } sfg_draw();
    } else if (get_event(evp, (1 << 1) | (1 << 3) | (1 << 5), 1)) {
        int what = rd16(evp); if (getenv("SFGDBG")) fprintf(stderr, "  [sfg] event %d v=%d h=%d\n", what, (int16_t)rd16(evp + 10), (int16_t)rd16(evp + 12));
        if (what == 1) {
            int gv = (int16_t)rd16(evp + 10), gh = (int16_t)rd16(evp + 12); Rc lr = sfg_list_rc();
            if (gh >= lr.l && gh < lr.r && gv >= lr.t && gv < lr.b) { int k = sfg.top + (gv - lr.t) / 16; if (k < sfg.n && (!sfg.put || sfg.isdir[k])) { uint32_t t = ticks_now(); int dbl = (k == sfg.last_item && t - sfg.last_tick < 25); sfg.sel = k; sfg.last_item = k; sfg.last_tick = t; sfg_draw(); if (dbl) openit = 1; } }
            else if (gh >= lr.r && gh < lr.r + 16 && gv >= lr.t && gv < lr.b) { int maxtop = sfg.n > SFG_ROWS ? sfg.n - SFG_ROWS : 0; int ty0, th0; sfg_thumb(&ty0, &th0); if (gv < lr.t + 16) sfg.top--; else if (gv >= lr.b - 16) sfg.top++; else if (sfg.n > SFG_ROWS && gv >= ty0 && gv < ty0 + th0) { sfg.track = 2; sfg.dragoff = gv - ty0; } else if (gv < ty0) sfg.top -= SFG_ROWS - 1; else sfg.top += SFG_ROWS - 1; if (sfg.top < 0) sfg.top = 0; if (sfg.top > maxtop) sfg.top = maxtop; sfg_draw(); }
            else { for (int k = 2; k < 4; k++) { Rc r = sfg_btn_rc(k); if (gh >= r.l && gh < r.r && gv >= r.t && gv < r.b && !(k == 2 && (sfg.put ? !sfg.nin[0] : sfg.sel < 0))) { sfg.btn = k + 1; sfg.track = 1; sfg_draw(); } }
                Rc tr = {sfg.box.t + 6, sfg.box.l + 14, sfg.box.t + 28, sfg.box.l + 226}; if (gh >= tr.l && gh < tr.r && gv >= tr.t && gv < tr.b && strcmp(sfg.dir, sfg.rootdir)) { char *sl = strrchr(sfg.dir, '/'); if (sl) { *sl = 0; sfg_scan(); sfg_draw(); } } }
        } else { int ch = rd32(evp + 2) & 0xFF, mods = rd16(evp + 14);
            if (sfg.put && ch != 13 && ch != 3 && ch != 27 && !(ch == '.' && (mods & 0x100)) && ch != 0x1E && ch != 0x1F) { int L = strlen(sfg.nin); if (ch == 8) { if (L) sfg.nin[L - 1] = 0; } else if (ch >= 32 && ch != 127 && !(mods & 0x100) && L < 31 && ch != ':') { sfg.nin[L] = ch; sfg.nin[L + 1] = 0; } sfg_draw(); }
            else if (ch == 13 || ch == 3) { if (sfg.put) { if (sfg.nin[0]) { sfg.result = 1; done = 1; } } else if (sfg.sel >= 0) openit = 1; }
            else if (ch == 27 || (ch == '.' && (mods & 0x100))) { sfg.result = 0; done = 1; }
            else if (ch == 0x1F || ch == 0x1E) { int k = sfg.sel < 0 ? 0 : sfg.sel + (ch == 0x1F ? 1 : -1); if (k >= 0 && k < sfg.n) { sfg.sel = k; if (k < sfg.top) sfg.top = k; if (k >= sfg.top + SFG_ROWS) sfg.top = k - SFG_ROWS + 1; sfg_draw(); } }
            else if (ch >= 32 && ch < 127) { for (int k = 0; k < sfg.n; k++) if (toupper((unsigned char)sfg.name[k][0]) >= toupper(ch)) { sfg.sel = k; if (k < sfg.top || k >= sfg.top + SFG_ROWS) sfg.top = k < sfg.n - SFG_ROWS ? k : (sfg.n > SFG_ROWS ? sfg.n - SFG_ROWS : 0); sfg_draw(); break; } } }
    }
    if (openit && sfg.sel >= 0) { if (sfg.isdir[sfg.sel]) { char nm[64]; strcpy(nm, sfg.name[sfg.sel]); sfg_enter(nm); sfg_draw(); }
        else { sfg.result = 1; done = 1; } }
    if (!done) { idle_hint++; rewind_trap(ret); return 0; }
    int sw = sfg.sa.r - sfg.sa.l, sh = sfg.sa.b - sfg.sa.t; for (int y = 0; y < sh; y++) memcpy(ram + FB_BASE + (sfg.sa.t + y) * SCR_W + sfg.sa.l, sfg.saved + y * sw, sw);
    free(sfg.saved); sfg.saved = NULL; gfx_dirty = 1; sfg.active = 0;
    wr8(reply, sfg.result); wr8(reply + 1, 0); wr32(reply + 2, 0); wr16(reply + 8, 1);
    if (sfg.result) { wr16(reply + 6, 0x100 + dir_id_for(sfg.dir)); const char *nm = sfg.put ? sfg.nin : sfg.name[sfg.sel]; int L = strlen(nm); if (L > 31) L = 31; wr8(reply + 10, L); for (int i = 0; i < L; i++) wr8(reply + 11 + i, nm[i]); }
    return 1;
}
