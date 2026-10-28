/* ---- Resource Manager: multiple resource files, search chain, add/write/update ---- */
static int rf_refnum(int fi) { return fi == 0 ? 1 : 100 + fi; }
static int rf_from_ref(int rn) { if (rn == 1) return 0; if (rn >= 101 && rn < 132 && rfiles[rn - 100].used) return rn - 100; return -1; }
static uint32_t slot_len(Res *r) { return r->handle ? hsize[slot_of(r->handle)] : r->len; }
static const uint8_t *res_bytes(Res *r) { return r->handle ? ram + rd32(r->handle) : r->buf + r->off; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void put16(uint8_t *p, uint32_t v) { p[0] = v >> 8; p[1] = v; }

static uint8_t *build_rsrc(int fi, size_t *outlen) {
    int idx[8192], n = 0; for (int i = 0; i < nres; i++) if (res[i].file == fi && !res[i].dead) idx[n++] = i;
    uint32_t types[256]; int nt = 0;
    for (int k = 0; k < n; k++) { int f = 0; for (int t = 0; t < nt; t++) if (types[t] == res[idx[k]].type) f = 1; if (!f && nt < 256) types[nt++] = res[idx[k]].type; }
    size_t dsz = 0; for (int k = 0; k < n; k++) dsz += 4 + slot_len(&res[idx[k]]);
    size_t names = 0; for (int k = 0; k < n; k++) if (res[idx[k]].name[0]) names += 1 + strlen(res[idx[k]].name);
    size_t tl = 2 + 8 * nt + 12 * n, mapsz = 28 + tl + names, total = 256 + dsz + mapsz;
    uint8_t *o = calloc(1, total + 16);
    put32(o, 256); put32(o + 4, 256 + dsz); put32(o + 8, dsz); put32(o + 12, mapsz);
    uint8_t *map = o + 256 + dsz; memcpy(map, o, 16); put16(map + 24, 28); put16(map + 26, 28 + tl);
    uint8_t *tlp = map + 28; put16(tlp, nt - 1 < 0 ? 0xFFFF : nt - 1);
    size_t dpos = 0, npos = 0, refp = 2 + 8 * nt; uint8_t *dp = o + 256;
    int dataoff[8192], nameoff[8192];
    for (int k = 0; k < n; k++) { Res *r = &res[idx[k]]; uint32_t L = slot_len(r); dataoff[k] = dpos; put32(dp + dpos, L); memcpy(dp + dpos + 4, res_bytes(r), L); dpos += 4 + L;
        if (r->name[0]) { nameoff[k] = npos; uint8_t *np = map + 28 + tl + npos; np[0] = strlen(r->name); memcpy(np + 1, r->name, np[0]); npos += 1 + np[0]; } else nameoff[k] = 0xFFFF; }
    for (int t = 0; t < nt; t++) {
        int cnt = 0; for (int k = 0; k < n; k++) if (res[idx[k]].type == types[t]) cnt++;
        uint8_t *te = tlp + 2 + 8 * t; put32(te, types[t]); put16(te + 4, cnt - 1); put16(te + 6, refp);
        for (int k = 0; k < n; k++) if (res[idx[k]].type == types[t]) {
            uint8_t *re = tlp + refp; put16(re, (uint16_t)res[idx[k]].id); put16(re + 2, nameoff[k]); re[4] = res[idx[k]].attrs & 0x7F;
            re[5] = dataoff[k] >> 16; re[6] = dataoff[k] >> 8; re[7] = dataoff[k]; refp += 12; }
    }
    *outlen = total; return o;
}
static uint16_t crc16(const uint8_t *p, int n) { uint16_t c = 0; for (int i = 0; i < n; i++) { c ^= (uint16_t)p[i] << 8; for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1; } return c; }
static void write_macbinary(const char *path, const char *name, const char *ftype, const char *creator, const uint8_t *data, long dlen, const uint8_t *rs, long rlen) {
    uint8_t h[128] = {0}; int nl = strlen(name); if (nl > 63) nl = 63; h[1] = nl; memcpy(h + 2, name, nl); memcpy(h + 65, ftype, 4); memcpy(h + 69, creator, 4);
    put32(h + 83, dlen); put32(h + 87, rlen); h[122] = 129; h[123] = 129; put16(h + 124, crc16(h, 124));
    FILE *f = fopen(path, "wb"); if (!f) { fprintf(stderr, "  cannot write %s\n", path); return; }
    fwrite(h, 1, 128, f); if (dlen) fwrite(data, 1, dlen, f); for (long i = dlen; i % 128; i++) fputc(0, f);
    if (rlen) fwrite(rs, 1, rlen, f); for (long i = rlen; i % 128; i++) fputc(0, f); fclose(f);
}
static void rf_flush(int fi) {
    if (fi <= 0 || !rfiles[fi].used || !rfiles[fi].changed) return;
    size_t rl; uint8_t *rs = build_rsrc(fi, &rl);
    long dl = 0, old_rl; uint8_t *dat = NULL; char nm[300] = "file"; const char *b = strrchr(rfiles[fi].path, '/'); strncpy(nm, b ? b + 1 : rfiles[fi].path, 299);
    size_t L = strlen(nm); if (L > 4 && !strcasecmp(nm + L - 4, ".bin")) nm[L - 4] = 0;
    if (mb_parse(rfiles[fi].path, &dl, &old_rl) && dl > 0) { dat = malloc(dl); FILE *f = fopen(rfiles[fi].path, "rb"); fseek(f, 128, SEEK_SET); if (fread(dat, 1, dl, f) != (size_t)dl) dl = 0; fclose(f); }
    write_macbinary(rfiles[fi].path, nm, "pref", "CSAC", dat, dl, rs, rl); free(dat); free(rs); rfiles[fi].changed = 0;
    fprintf(stderr, "  [rm] wrote %s (%zu bytes of resources)\n", rfiles[fi].path, rl);
}
static void rm_flush_all(void) { for (int i = 1; i < 32; i++) rf_flush(i); }
static uint32_t res_handle_of(Res *r) { return r ? res_get_handle(r) : 0; }
static void pstring(uint32_t a, char *out, int max) { int l = a ? rd8(a) : 0; if (l >= max) l = max - 1; for (int i = 0; i < l; i++) out[i] = rd8(a + 1 + i); out[l] = 0; }

static int rm_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    uint32_t sp = SP;
    switch (n) {
    case 0xA997: {          /* OpenResFile(name): refnum */
        char path[1100], bin[1200]; int isdir; if (resolve(0, rd32(sp), path, &isdir) || isdir) { setsp(sp + 4); wr16(sp + 4, 0xFFFF); res_err = -43; return 1; }
        size_t L = strlen(path); const char *src = path; if (!(L > 4 && !strcasecmp(path + L - 4, ".bin"))) { snprintf(bin, sizeof bin, "%s.bin", path); src = bin; }
        long dl, rl; if (!mb_parse(src, &dl, &rl)) { setsp(sp + 4); wr16(sp + 4, 0xFFFF); res_err = -39; return 1; }
        int fi = -1; for (int i = 1; i < 32; i++) if (!rfiles[i].used) { fi = i; break; } if (fi < 0) { setsp(sp + 4); wr16(sp + 4, 0xFFFF); res_err = -42; return 1; }
        RFile *rf = &rfiles[fi]; memset(rf, 0, sizeof *rf); rf->used = 1; strncpy(rf->path, src, sizeof rf->path - 1);
        if (rl > 0) { rf->buf = malloc(rl); FILE *f = fopen(src, "rb"); fseek(f, 128 + ((dl + 127) / 128) * 128, SEEK_SET); if (fread(rf->buf, 1, rl, f) != (size_t)rl) rl = 0; fclose(f); rf->len = rl; if (rl) parse_rsrc(rf->buf, rl, fi); }
        chain[nchain++] = fi; cur_rf = fi; res_err = 0; setsp(sp + 4); wr16(sp + 4, rf_refnum(fi));
        fprintf(stderr, "  OpenResFile -> refnum %d (%s, %ld bytes rsrc)\n", rf_refnum(fi), src, rl); return 1; }
    case 0xA9B1: {          /* CreateResFile(name) */
        char nm[256]; pstring(rd32(sp), nm, 256); setsp(sp + 4); fs_init();
        const char *dir = dir_path(cur_dir); char leaf[256]; strcpy(leaf, nm); char *c = strrchr(nm, ':'); char dpath[1100]; strncpy(dpath, dir, sizeof dpath);
        if (c) { strcpy(leaf, c + 1); *c = 0; char tmp[1100]; int isd; uint32_t save = rd32(sp); (void)save; (void)tmp; (void)isd; }
        char full[1300]; snprintf(full, sizeof full, "%s/%s.bin", dpath, leaf); char plain[1300]; snprintf(plain, sizeof plain, "%s/%s", dpath, leaf);
        struct stat st; if (stat(full, &st) && stat(plain, &st)) write_macbinary(full, leaf, "pref", "CSAC", NULL, 0, NULL, 0);
        fprintf(stderr, "  CreateResFile(\"%s\") -> %s\n", leaf, full); res_err = 0; return 1; }
    case 0xA99A: case 0xA999: {      /* CloseResFile / UpdateResFile */
        int fi = rf_from_ref((int16_t)rd16(sp)); setsp(sp + 2); if (fi > 0) { rf_flush(fi);
            if (n == 0xA99A) { for (int i = 0; i < nres; i++) if (res[i].file == fi) res[i].dead = 1; rfiles[fi].used = 0; int k = 0; for (int c = 0; c < nchain; c++) if (chain[c] != fi) chain[k++] = chain[c]; nchain = k; if (cur_rf == fi) cur_rf = chain[nchain - 1]; } }
        res_err = 0; return 1; }
    case 0xA998: { int fi = rf_from_ref((int16_t)rd16(sp)); setsp(sp + 2); if (fi >= 0) cur_rf = fi; return 1; }          /* UseResFile */
    case 0xA994: setsp(sp); wr16(sp, rf_refnum(cur_rf)); return 1;                                                         /* CurResFile */
    case 0xA9A4: { Res *r = res_from_handle(rd32(sp)); setsp(sp + 4); wr16(sp + 4, r ? rf_refnum(r->file) : 1); return 1; } /* HomeResFile */
    case 0xA9AB: {          /* AddResource(h, type, id, name) */
        uint32_t np = rd32(sp), type = rd32(sp + 6), h = rd32(sp + 10); int16_t id = rd16(sp + 4);
        if (nres >= 8192) { setsp(sp + 14); return 1; }
        Res *r = &res[nres++]; memset(r, 0, sizeof *r); r->type = type; r->id = id; r->handle = h; r->len = hsize[slot_of(h)]; r->file = cur_rf; r->buf = NULL; pstring(np, r->name, 64);
        rfiles[cur_rf].changed = 1; setsp(sp + 14); res_err = 0; return 1; }
    case 0xA9B0: case 0xA9AA: { Res *r = res_from_handle(rd32(sp)); if (r) rfiles[r->file].changed = 1; setsp(sp + 4); res_err = 0; return 1; }  /* WriteResource / ChangedResource */
    case 0xA9AD: { Res *r = res_from_handle(rd32(sp)); if (r) { r->dead = 1; rfiles[r->file].changed = 1; } setsp(sp + 4); return 1; }             /* RmveResource */
    case 0xA9A7: { Res *r = res_from_handle(rd32(sp + 2)); if (r) r->attrs = rd16(sp) & 0xFF; setsp(sp + 6); return 1; }                               /* SetResAttrs */
    case 0xA9A8: {          /* GetResInfo(h, VAR id, VAR type, VAR name) */
        Res *r = res_from_handle(rd32(sp + 12)); if (r) { if (rd32(sp + 8)) wr16(rd32(sp + 8), (uint16_t)r->id); if (rd32(sp + 4)) wr32(rd32(sp + 4), r->type); if (rd32(sp)) { wr8(rd32(sp), strlen(r->name)); for (size_t i = 0; i < strlen(r->name); i++) wr8(rd32(sp) + 1 + i, r->name[i]); } }
        setsp(sp + 16); return 1; }
    case 0xA9A9: { Res *r = res_from_handle(rd32(sp + 6)); if (r) { r->id = rd16(sp + 4); pstring(rd32(sp), r->name, 64); rfiles[r->file].changed = 1; } setsp(sp + 10); return 1; } /* SetResInfo */
    case 0xA9C1: { uint32_t type = rd32(sp); int id = 128; for (;; id++) { int used = 0; for (int i = 0; i < nres; i++) if (!res[i].dead && res[i].type == type && res[i].id == id) used = 1; if (!used) break; } setsp(sp + 4); wr16(sp + 4, id); return 1; }
    case 0xA9A0: case 0xA81F: {      /* GetResource / Get1Resource */
        uint32_t id = rd16(sp), type = rd32(sp + 2); Res *r = n == 0xA9A0 ? find_res(type, (int16_t)id) : find_res_1(type, (int16_t)id); setsp(sp + 6); wr32(sp + 6, res_handle_of(r)); res_err = r ? 0 : -192; return 1; }
    case 0xA9A1: case 0xA820: {      /* Get(1)NamedResource(type, name) */
        char nm[256]; pstring(rd32(sp), nm, 256); uint32_t type = rd32(sp + 4); Res *f = NULL;
        for (int c = nchain - 1; c >= 0 && !f; c--) { if (n == 0xA820 && chain[c] != cur_rf) continue; for (int i = 0; i < nres; i++) if (res[i].file == chain[c] && !res[i].dead && res[i].type == type && !strcasecmp(res[i].name, nm)) { f = &res[i]; break; } }
        setsp(sp + 8); wr32(sp + 8, res_handle_of(f)); res_err = f ? 0 : -192; return 1; }
    case 0xA99C: case 0xA80D: {      /* Count(1)Resources */
        uint32_t type = rd32(sp); int c = 0; for (int i = 0; i < nres; i++) if (!res[i].dead && res[i].type == type && (n == 0xA99C || res[i].file == cur_rf) && (n == 0xA80D || rf_from_ref(rf_refnum(res[i].file)) >= 0)) c++; setsp(sp + 4); wr16(sp + 4, c); return 1; }
    case 0xA99D: case 0xA80E: {      /* Get(1)IndResource(type, index) */
        uint32_t type = rd32(sp), idx = rd16(sp + 4); int c = 0; Res *f = NULL;
        for (int cc = nchain - 1; cc >= 0 && !f; cc--) { if (n == 0xA80E && chain[cc] != cur_rf) continue; for (int i = 0; i < nres; i++) if (!res[i].dead && res[i].file == chain[cc] && res[i].type == type && ++c == (int)idx) { f = &res[i]; break; } }
        setsp(sp + 6); wr32(sp + 6, res_handle_of(f)); return 1; }
    case 0xA99E: case 0xA81C: case 0xA99F: case 0xA80F: {      /* Count(1)Types / Get(1)IndType */
        uint32_t seen[128]; int c = 0; for (int i = 0; i < nres; i++) { if (res[i].dead || ((n == 0xA81C || n == 0xA80F) && res[i].file != cur_rf)) continue; int f = 0; for (int j = 0; j < c; j++) if (seen[j] == res[i].type) f = 1; if (!f && c < 128) seen[c++] = res[i].type; }
        if (n == 0xA99E || n == 0xA81C) { setsp(sp + 0); wr16(sp, c); } else { int idx = (int16_t)rd16(sp); uint32_t pt = rd32(sp + 2); if (idx >= 1 && idx <= c) wr32(pt, seen[idx - 1]); else wr32(pt, 0); setsp(sp + 6); }
        return 1; }
    case 0xA9AF: wr16(sp, (uint16_t)res_err); return 1;                                                                    /* ResError */
    default: return 0;
    }
}
