/* ---- QuickTime Movie Toolbox (minimal): flat Cinepak movies (data fork) + 'moov' resource (resource fork) ---- */
typedef struct { uint32_t off, size, t; } MSample;
typedef struct { uint8_t y[4]; int8_t u, v; uint8_t rgb[4][3]; } CvCode;
typedef struct {
    int used; uint32_t rec; char datapath[1100]; uint8_t *data; long datalen;
    uint32_t mscale, mdur, ts, dur; int ns; MSample *sm; int w, h; uint32_t port; Rc box; int playing; double start_ms; int cur; uint8_t *fb; CvCode v1[256], v4[256]; uint32_t last_draw_cur;
} Movie;
static Movie movies[8];
typedef struct { int used; char path[1100]; } MFile;
static MFile mfiles[8];

static uint8_t *read_whole(const char *path, int rf, long *len) {
    int s = open_fork(path, rf, 0); if (s < 0) return NULL; OF *o = &ofiles[s]; uint8_t *b = malloc(o->len ? o->len : 1); fseek(o->f, o->base, SEEK_SET); long n = fread(b, 1, o->len, o->f); *len = n; fclose(o->f); o->f = NULL; return b;
}
static const uint8_t *atom_find(const uint8_t *b, long len, const char *t, long *alen) {       /* search direct children */
    long o = 0; while (o + 8 <= len) { uint32_t sz = be32(b + o); if (sz < 8 || o + sz > len) break; if (!memcmp(b + o + 4, t, 4)) { *alen = sz - 8; return b + o + 8; } o += sz; } return NULL;
}
static int movie_load(Movie *m, const char *path) {
    long rl = 0; uint8_t *rs = read_whole(path, 1, &rl); if (!rs || rl < 16) { free(rs); return -1; }
    uint32_t doff = be32(rs), moff = be32(rs + 4); const uint8_t *map = rs + moff; uint32_t tl = be16(map + 24); const uint8_t *tbl = map + tl; int nt = (int)be16(tbl) + 1; const uint8_t *moov = NULL; long moovlen = 0;
    for (int i = 0; i < nt; i++) { const uint8_t *e = tbl + 2 + i * 8; if (!memcmp(e, "moov", 4)) { uint32_t ro = be16(e + 6); const uint8_t *rr = tbl + ro; uint32_t dataoff = be32(rr + 4) & 0xFFFFFF; moovlen = be32(rs + doff + dataoff); moov = rs + doff + dataoff + 4; break; } }
    if (!moov) { free(rs); return -1; }
    long mlen; const uint8_t *mvhd = atom_find(moov, moovlen, "mvhd", &mlen); if (mvhd) { m->ts = be32(mvhd + 12); m->dur = be32(mvhd + 16); }
    long tlen; const uint8_t *trak = atom_find(moov, moovlen, "trak", &tlen); if (!trak) { free(rs); return -1; }
    const uint8_t *tkhd = atom_find(trak, tlen, "tkhd", &mlen); if (tkhd) { m->w = be16(tkhd + 76); m->h = be16(tkhd + 80); }
    long mdl; const uint8_t *mdia = atom_find(trak, tlen, "mdia", &mdl); const uint8_t *mdhd = atom_find(mdia, mdl, "mdhd", &mlen); if (mdhd) { m->mscale = be32(mdhd + 12); m->mdur = be32(mdhd + 16); }
    long minl; const uint8_t *minf = atom_find(mdia, mdl, "minf", &minl); long stl; const uint8_t *stbl = atom_find(minf, minl, "stbl", &stl);
    long l1; const uint8_t *stsd = atom_find(stbl, stl, "stsd", &l1); if (stsd && memcmp(stsd + 12, "cvid", 4)) fprintf(stderr, "  [qt] unsupported codec %.4s\n", stsd + 12);
    if (stsd) { m->w = be16(stsd + 8 + 32); m->h = be16(stsd + 8 + 34); }
    const uint8_t *stts = atom_find(stbl, stl, "stts", &l1), *stsc = NULL, *stsz = NULL, *stco = NULL; long lstsc, lstsz, lstco, lstts = l1; stsc = atom_find(stbl, stl, "stsc", &lstsc); stsz = atom_find(stbl, stl, "stsz", &lstsz); stco = atom_find(stbl, stl, "stco", &lstco);
    if (!stts || !stsc || !stsz || !stco) { free(rs); return -1; }
    uint32_t nsz = be32(stsz + 8), cs = be32(stsz + 4); m->ns = nsz; m->sm = calloc(nsz + 1, sizeof(MSample));
    for (uint32_t i = 0; i < nsz; i++) m->sm[i].size = cs ? cs : be32(stsz + 12 + 4 * i);
    uint32_t nsc = be32(stsc + 4), nco = be32(stco + 4), si = 0;
    for (uint32_t c = 1; c <= nco && si < nsz; c++) {
        uint32_t spc = 1; for (uint32_t k = 0; k < nsc; k++) if (be32(stsc + 8 + k * 12) <= c) spc = be32(stsc + 8 + k * 12 + 4);
        uint32_t off = be32(stco + 8 + 4 * (c - 1)); for (uint32_t k = 0; k < spc && si < nsz; k++) { m->sm[si].off = off; off += m->sm[si].size; si++; }
    }
    uint32_t nts = be32(stts + 4), t = 0; si = 0; for (uint32_t k = 0; k < nts; k++) { uint32_t cnt = be32(stts + 8 + k * 8), du = be32(stts + 12 + k * 8); for (uint32_t j = 0; j < cnt && si < nsz; j++) { m->sm[si++].t = t; t += du; } }
    m->mdur = t ? t : m->mdur; free(rs);
    m->data = read_whole(path, 0, &m->datalen); if (!m->data) return -1;
    m->fb = calloc(m->w * m->h, 3); m->cur = -1; m->box = (Rc){0, 0, m->h, m->w};
    return 0;
}
/* ---- Cinepak ---- */
static void cv_codebook(CvCode *cb, int id, const uint8_t *d, int size) {
    int gray = id & 0x0400, sel = id & 0x0100; const uint8_t *e = d + size; uint32_t flag = 0, mask = 0;
    for (int i = 0; i < 256; i++) {
        if (sel && !(mask >>= 1)) { if (d + 4 > e) break; flag = be32(d); d += 4; mask = 0x80000000u; }
        if (!sel || (flag & mask)) {
            int need = gray ? 4 : 6; if (d + need > e) break;
            for (int k = 0; k < 4; k++) cb[i].y[k] = *d++;
            if (!gray) { cb[i].u = (int8_t)*d++; cb[i].v = (int8_t)*d++; } else { cb[i].u = cb[i].v = 0; }
            for (int k = 0; k < 4; k++) { int y = cb[i].y[k], r, g, b;
                if (gray) r = g = b = y; else { r = y + cb[i].v * 2; g = y - (cb[i].u / 2) - cb[i].v; b = y + cb[i].u * 2; }
                cb[i].rgb[k][0] = r < 0 ? 0 : r > 255 ? 255 : r; cb[i].rgb[k][1] = g < 0 ? 0 : g > 255 ? 255 : g; cb[i].rgb[k][2] = b < 0 ? 0 : b > 255 ? 255 : b; }
        }
    }
}
static void cv_put(Movie *m, int x, int y, const uint8_t *rgb) { if (x < 0 || y < 0 || x >= m->w || y >= m->h) return; uint8_t *p = m->fb + (y * m->w + x) * 3; p[0] = rgb[0]; p[1] = rgb[1]; p[2] = rgb[2]; }
static void cv_decode(Movie *m, const uint8_t *d, uint32_t len) {
    if (len < 10) return; int strips = be16(d + 8); const uint8_t *p = d + 10, *end = d + len; int y0 = 0; CvCode *v1 = m->v1, *v4 = m->v4; (void)v1; (void)v4;
    static CvCode sv1[256], sv4[256];
    for (int s = 0; s < strips && p + 12 <= end; s++) {
        int sid = be16(p), ssize = be16(p + 2); int sh = (int)be16(p + 8) - (int)be16(p + 4); int y1 = y0, y2 = y0 + sh; y0 = y2; const uint8_t *sp = p + 12, *se = p + ssize; if (se > end) se = end;
        if (s > 0) { /* codebooks carry over from the previous strip (kept in sv1/sv4) */ }
        (void)sid;
        while (sp + 4 <= se) {
            int cid = be16(sp), csz = be16(sp + 2); const uint8_t *cd = sp + 4; if (csz < 4) break; int cl = csz - 4; if (sp + csz > se) cl = (int)(se - cd);
            switch (cid) {
            case 0x2000: case 0x2100: case 0x2400: case 0x2500: cv_codebook(sv4, cid, cd, cl); break;
            case 0x2200: case 0x2300: case 0x2600: case 0x2700: cv_codebook(sv1, cid, cd, cl); break;
            case 0x3000: case 0x3100: case 0x3200: {
                uint32_t flag = 0, mask = 0; const uint8_t *q = cd, *qe = cd + cl;
                for (int y = y1; y < y2; y += 4) for (int x = 0; x < m->w; x += 4) {
                    if ((cid & 0x0100) && !(mask >>= 1)) { if (q + 4 > qe) goto strip_done; flag = be32(q); q += 4; mask = 0x80000000u; }
                    if (!(cid & 0x0100) || (flag & mask)) {
                        if (!(cid & 0x0200) && !(mask >>= 1)) { if (q + 4 > qe) goto strip_done; flag = be32(q); q += 4; mask = 0x80000000u; }
                        if ((cid & 0x0200) || (~flag & mask)) {
                            if (q >= qe) goto strip_done; CvCode *c = &sv1[*q++];
                            for (int k = 0; k < 4; k++) { int bx = x + (k & 1) * 2, by = y + (k >> 1) * 2; cv_put(m, bx, by, c->rgb[k]); cv_put(m, bx + 1, by, c->rgb[k]); cv_put(m, bx, by + 1, c->rgb[k]); cv_put(m, bx + 1, by + 1, c->rgb[k]); }
                        } else {
                            for (int k = 0; k < 4; k++) { if (q >= qe) goto strip_done; CvCode *c = &sv4[*q++]; int bx = x + (k & 1) * 2, by = y + (k >> 1) * 2;
                                cv_put(m, bx, by, c->rgb[0]); cv_put(m, bx + 1, by, c->rgb[1]); cv_put(m, bx, by + 1, c->rgb[2]); cv_put(m, bx + 1, by + 1, c->rgb[3]); }
                        }
                    }
                }
                strip_done: break; }
            default: break;
            }
            sp += csz;
        }
        p += ssize;
    }
}
static void movie_draw(Movie *m) {
    uint32_t port = m->port ? m->port : the_port(); if (!port) return; DC d; dc_init(&d, port); Rc dst = m->box; int dw = dst.r - dst.l, dh = dst.b - dst.t; if (dw <= 0 || dh <= 0) { dc_free(&d); return; }
    for (int ci = 0; ci < d.clip.n; ci++) { Rc c = d.clip.r[ci]; int y0 = c.t > dst.t ? c.t : dst.t, y1 = c.b < dst.b ? c.b : dst.b, x0 = c.l > dst.l ? c.l : dst.l, x1 = c.r < dst.r ? c.r : dst.r;
        for (int y = y0; y < y1; y++) { int sy = (y - dst.t) * m->h / dh; for (int x = x0; x < x1; x++) { int sx = (x - dst.l) * m->w / dw; const uint8_t *px = m->fb + (sy * m->w + sx) * 3; RGB col = {px[0] * 257, px[1] * 257, px[2] * 257}; pp(&d.s, x, y, rgb_px(&d.s, col)); } } }
    dc_free(&d); gfx_dirty = 1;
}
static Movie *movie_of(uint32_t rec) { for (int i = 0; i < 8; i++) if (movies[i].used && movies[i].rec == rec) return &movies[i]; return NULL; }
static int movie_frame_at(Movie *m, uint32_t tmedia) { int lo = 0; for (int i = 0; i < m->ns; i++) { if (m->sm[i].t <= tmedia) lo = i; else break; } return lo; }
static void movie_task(Movie *m) {
    if (!m->playing) return; double el = now_ms() - m->start_ms; uint32_t tm = (uint32_t)(el * m->mscale / 1000.0);
    int want = movie_frame_at(m, tm); if (tm >= m->mdur) want = m->ns - 1;
    if (want > m->cur) { for (int i = m->cur + 1; i <= want; i++) { MSample *s = &m->sm[i]; if (s->off + s->size <= (uint32_t)m->datalen) cv_decode(m, m->data + s->off, s->size); } m->cur = want; movie_draw(m); }
}
static int qt_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    if (n != 0xAAAA) return 0; uint32_t sp = SP; int sel = D(0) & 0xFFFF;
    switch (sel) {
    case 0x0001: wr16(sp, 0); return 1;                                  /* EnterMovies */
    case 0x0002: return 1;                                               /* ExitMovies */
    case 0x0005: { uint32_t mv = rd32(sp + 4); setsp(sp + 8); Movie *m = movie_of(mv); if (m) movie_task(m); return 1; }      /* MoviesTask */
    case 0x000B: { uint32_t mv = rd32(sp); setsp(sp + 4); Movie *m = movie_of(mv); if (m) { m->playing = 1; m->start_ms = now_ms(); m->cur = -1; movie_task(m); } return 1; }      /* StartMovie */
    case 0x000C: { uint32_t mv = rd32(sp); setsp(sp + 4); Movie *m = movie_of(mv); if (m) m->playing = 0; return 1; }      /* StopMovie */
    case 0x0016: { uint32_t mv = rd32(sp + 8), port = rd32(sp + 4); setsp(sp + 12); Movie *m = movie_of(mv); if (m) m->port = port; return 1; }      /* SetMovieGWorld */
    case 0x00D5: { int rn = rd16(sp); setsp(sp + 2); wr16(sp + 2, 0); if (rn >= 0x300 && rn < 0x308) mfiles[rn - 0x300].used = 0; return 1; }      /* CloseMovieFile */
    case 0x00DD: { uint32_t mv = rd32(sp); setsp(sp + 4); Movie *m = movie_of(mv); int done = 1; if (m) { double el = now_ms() - m->start_ms; done = !m->playing || (uint32_t)(el * m->mscale / 1000.0) >= m->mdur; if (done) m->playing = 0; } wr_bool(sp + 4, done); return 1; }      /* IsMovieDone */
    case 0x00F0: {      /* NewMovieFromFile(&movie, refnum, &resid, resname, flags, &changed) */
        uint32_t changed = rd32(sp), mvp = rd32(sp + 16); int rn = rd16(sp + 14); setsp(sp + 20); int err = -43;
        if (changed) wr8(changed, 0);
        if (rn >= 0x300 && rn < 0x308 && mfiles[rn - 0x300].used) { Movie *m = NULL; for (int i = 0; i < 8; i++) if (!movies[i].used) { m = &movies[i]; break; }
            if (m) { memset(m, 0, sizeof *m); err = movie_load(m, mfiles[rn - 0x300].path); if (err == 0) { m->used = 1; m->rec = alloc_block(16); wr32(m->rec, 0x4D4F5649); wr32(mvp, m->rec); } else { wr32(mvp, 0); free(m->data); free(m->sm); free(m->fb); } } else err = -108; }
        wr16(sp + 20, err); return 1; }
    case 0x00F9: case 0x00FA: { uint32_t boxp = rd32(sp), mv = rd32(sp + 4); setsp(sp + 8); Movie *m = movie_of(mv); if (m) { if (sel == 0x00F9) rc_put(boxp, m->box); else m->box = rc_get(boxp); } return 1; }      /* Get/SetMovieBox */
    case 0x0192: {      /* OpenMovieFile(FSSpec*, &refnum, perm) */
        uint32_t spec = rd32(sp + 6), rnp = rd32(sp + 2); setsp(sp + 10); int err = -43; char nm[80]; pstr(spec + 6, nm, 64); uint32_t parid = rd32(spec + 2); const char *base = dir_path(parid ? parid : cur_dir); char path[1100]; int isdir;
        if (base && lookup(base, nm, path, &isdir, 1)) { for (int i = 0; i < 8; i++) if (!mfiles[i].used) { mfiles[i].used = 1; size_t L = strlen(path); if (L > 4 && !strcasecmp(path + L - 4, ".bin")) path[L - 4] = 0; strncpy(mfiles[i].path, path, 1099); wr16(rnp, 0x300 + i); err = 0; break; } }
        wr16(sp + 10, err); return 1; }
    default: fprintf(stderr, "  [qt] selector %04x not implemented\n", sel); return 0;
    }
}

/* ---- AppleEvents / Alias / HighLevelFS / misc stubs ---- */
static int misc_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    uint32_t sp = SP; int sel = D(0) & 0xFFFF;
    switch (n) {
    case 0xA816: { int pop = (sel >> 8) * 2; setsp(sp + pop); wr16(sp + pop, sel == 0x091F ? 0 : (uint16_t)-1701); return 1; }      /* AEDispatch: no events delivered */
    case 0xA823:
        if (sel == 0) { uint32_t dp = rd32(sp), vp = rd32(sp + 4); setsp(sp + 16); if (vp) wr16(vp, (uint16_t)-1); if (dp) wr32(dp, cur_dir); wr16(sp + 16, 0); return 1; }      /* FindFolder */
        return 0;
    case 0xAA52:
        if ((sel & 0xF) == 1) {      /* FSMakeFSSpec(vRef, dirID, name, spec) */
            uint32_t spec = rd32(sp), name = rd32(sp + 4), dirid = rd32(sp + 8); setsp(sp + 14); char nm[80]; pstr(name, nm, 64); char *c = strrchr(nm, ':'); const char *leaf = c ? c + 1 : nm;
            const char *base = dir_path(dirid ? dirid : cur_dir); char path[1100]; int isdir, err = base ? 0 : -43;
            if (base && *leaf && !lookup(base, leaf, path, &isdir, 1)) err = -43;
            wr16(spec, (uint16_t)-1); wr32(spec + 2, dirid ? dirid : cur_dir); int L = strlen(leaf); if (L > 63) L = 63; wr8(spec + 6, L); for (int i = 0; i < L; i++) wr8(spec + 7 + i, leaf[i]);
            wr16(sp + 14, (uint16_t)err); return 1;
        }
        return 0;
    case 0xA83B: setsp(sp + 8); wr_bool(sp + 8, 0); return 1;      /* TrackBox */
    case 0xA8C3: { uint32_t ap = rd32(sp); setsp(sp + 12); wr16(ap, 0); return 1; }      /* PtToAngle */
    case 0xA9FF: case 0xABFF: return 1;      /* Debugger / DebugStr */
    }
    return 0;
}

/* ---- Pack3 Standard File: non-interactive (auto-accept save with suggested name, cancel opens) ---- */
static int sf_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    if (n != 0xA9EA) return 0; uint32_t sp = SP; int sel = rd16(sp);
    switch (sel) {
    case 1: case 3: {      /* SFPutFile / SFPPutFile */
        uint32_t reply = rd32(sp + 2), orig = rd32(sp + 10); int pop = sel == 1 ? 22 : 28; setsp(sp + pop);
        char nm[80]; pstr(orig, nm, 64); if (!*nm) strcpy(nm, "Untitled");
        wr8(reply, 1); wr8(reply + 1, 0); wr32(reply + 2, 0); wr16(reply + 6, (uint16_t)cur_vref); wr16(reply + 8, 0); int L = strlen(nm); wr8(reply + 10, L); for (int i = 0; i < L; i++) wr8(reply + 11 + i, nm[i]);
        fprintf(stderr, "  [sf] SFPutFile -> '%s'\n", nm); return 1; }
    case 2: { uint32_t reply = rd32(sp + 2), where = rd32(sp + 24); if (!sfg_step(ret, reply, (int16_t)(where >> 16), (int16_t)(where & 0xFFFF))) return 1; setsp(sp + 28); return 1; }
    case 4: { uint32_t reply = rd32(sp + 2); setsp(sp + 34); wr8(reply, 0); fprintf(stderr, "  [sf] SFPGetFile -> cancel\n"); return 1; }
    }
    return 0;
}
