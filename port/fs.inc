/* ---------- File Manager HLE: maps HFS calls onto the host game folder ---------- */
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>
#include <unistd.h>
#define MAXDIRS 256
#define MAXOPEN 64
static char game_root[1024] = ".";
static const char *volname = "Castles";
static char *dirpath[MAXDIRS]; static int ndirs = 0;          /* dirID = 2 + index */
typedef struct { FILE *f; char path[1100]; int rf; long base, len, mark; } OF;
static OF ofiles[MAXOPEN];
static int fs_init_done = 0;
static uint32_t cur_dir = 2; static int16_t cur_vref = -1;

static void fs_init(void) { if (fs_init_done) return; fs_init_done = 1; dirpath[0] = strdup(game_root); ndirs = 1; }
static uint32_t dir_id_for(const char *p) { for (int i = 0; i < ndirs; i++) if (!strcmp(dirpath[i], p)) return 2 + i; if (ndirs >= MAXDIRS) return 2; dirpath[ndirs] = strdup(p); return 2 + ndirs++; }
static const char *dir_path(uint32_t id) { fs_init(); return (id >= 2 && id - 2 < (uint32_t)ndirs) ? dirpath[id - 2] : NULL; }
static void pstr(uint32_t a, char *out, int max) { int n = rd8(a); if (n >= max) n = max - 1; for (int i = 0; i < n; i++) out[i] = rd8(a + 1 + i); out[n] = 0; }
static void put_pstr(uint32_t a, const char *s) { int n = strlen(s); if (n > 255) n = 255; wr8(a, n); for (int i = 0; i < n; i++) wr8(a + 1 + i, s[i]); }

/* case-insensitive lookup of one component in dir; returns 1 and fills out (full path) if found */
static int lookup(const char *dir, const char *name, char *out, int *isdir, int try_bin) {
    DIR *d = opendir(dir); if (!d) return 0; struct dirent *e; int found = 0;
    char bin[300]; snprintf(bin, sizeof bin, "%s.bin", name);
    while ((e = readdir(d))) {
        if (!strcasecmp(e->d_name, name) || (try_bin && !strcasecmp(e->d_name, bin))) {
            snprintf(out, 1100, "%s/%s", dir, e->d_name); struct stat st; stat(out, &st); *isdir = S_ISDIR(st.st_mode); found = 1;
            if (!strcasecmp(e->d_name, name)) break;     /* exact match wins */
        }
    }
    closedir(d); return found;
}
/* Resolve HFS name relative to dirID. Returns 0 ok, else OSErr. Sets *path, *isdir. */
static int resolve(uint32_t dirid, uint32_t nameptr, char *path, int *isdir) {
    fs_init(); char name[512] = ""; if (nameptr) pstr(nameptr, name, sizeof name);
    const char *base = dir_path(dirid ? dirid : cur_dir); if (!base) return -120;
    char cur[1100]; strncpy(cur, base, sizeof cur); *isdir = 1;
    char *p = name;
    if (*p && *p != ':' && strchr(p, ':')) { p = strchr(p, ':') + 1; }          /* leading volume name */
    else if (*p == ':') p++;
    while (*p) {
        char comp[300]; int n = 0; while (*p && *p != ':' && n < 299) comp[n++] = *p++; comp[n] = 0; if (*p == ':') p++;
        if (!n) continue;
        if (!*isdir) return -120;
        char nxt[1100]; int di; if (!lookup(cur, comp, nxt, &di, 1)) return *p ? -120 : -43;
        strncpy(cur, nxt, sizeof cur); *isdir = di;
    }
    strncpy(path, cur, 1100); return 0;
}
static long fsize(const char *p) { struct stat st; return stat(p, &st) ? -1 : st.st_size; }
static int mb_parse(const char *p, long *dlen, long *rlen) {
    FILE *f = fopen(p, "rb"); if (!f) return 0; uint8_t h[128]; int ok = fread(h, 1, 128, f) == 128; fclose(f); if (!ok) return 0;
    *dlen = be32(h + 83); *rlen = be32(h + 87); return 1;
}
static int open_fork(const char *path, int rf, int wr) {
    int slot = -1; for (int i = 1; i < MAXOPEN; i++) if (!ofiles[i].f) { slot = i; break; } if (slot < 0) return -42;
    OF *o = &ofiles[slot]; memset(o, 0, sizeof *o); o->rf = rf; strncpy(o->path, path, sizeof o->path - 1);
    size_t L = strlen(path); int isbin = L > 4 && !strcasecmp(path + L - 4, ".bin");
    char other[1200];
    if (!rf && !isbin) { o->f = fopen(path, wr ? "r+b" : "rb"); if (!o->f) return wr ? -61 : -43; o->base = 0; o->len = fsize(path); }
    else {
        const char *src = path;
        if (!isbin) { snprintf(other, sizeof other, "%s.bin", path); src = other; }
        long dl, rl; if (!mb_parse(src, &dl, &rl)) return rf ? -43 : -43;
        o->f = fopen(src, "rb"); if (!o->f) return -43;
        if (rf) { o->base = 128 + ((dl + 127) / 128) * 128; o->len = rl; } else { o->base = 128; o->len = dl; }
    }
    return slot;
}
static void fill_cat(uint32_t pb, const char *path, int isdir, uint32_t parent) {
    const char *nm = strrchr(path, '/'); nm = nm ? nm + 1 : path; char clean[300]; strncpy(clean, nm, 299); clean[299] = 0;
    size_t L = strlen(clean); if (L > 4 && !strcasecmp(clean + L - 4, ".bin")) clean[L - 4] = 0;
    if (rd32(pb + 18)) put_pstr(rd32(pb + 18), clean);
    wr8(pb + 30, isdir ? 0x10 : 0);
    if (isdir) { wr32(pb + 48, dir_id_for(path)); wr32(pb + 100, parent); }
    else {
        long dl = fsize(path), rl = 0; char b[1200]; size_t l2 = strlen(path);
        if (l2 > 4 && !strcasecmp(path + l2 - 4, ".bin")) { long a, c; if (mb_parse(path, &a, &c)) { dl = a; rl = c; } }
        else { snprintf(b, sizeof b, "%s.bin", path); long a, c; if (mb_parse(b, &a, &c)) rl = c; }
        wr32(pb + 54, dl); wr32(pb + 58, (dl + 511) / 512 * 512); wr32(pb + 64, rl); wr32(pb + 68, (rl + 511) / 512 * 512);
        wr32(pb + 100, parent);
    }
}
static int dir_of_path(const char *path, char *parent) { strncpy(parent, path, 1100); char *s = strrchr(parent, '/'); if (!s) return 0; *s = 0; return 1; }


static int enum_dir(const char *dir, int index, char *out) {      /* nth visible entry (1-based) */
    DIR *d = opendir(dir); if (!d) return 0; struct dirent *e; int c = 0, found = 0; char names[4096][96]; int nn = 0;
    while ((e = readdir(d)) && nn < 4096) { if (e->d_name[0] == '.') continue; strncpy(names[nn], e->d_name, 95); names[nn][95] = 0; nn++; }
    closedir(d);
    for (int i = 0; i < nn - 1; i++) for (int j = i + 1; j < nn; j++) if (strcasecmp(names[i], names[j]) > 0) { char t[96]; strcpy(t, names[i]); strcpy(names[i], names[j]); strcpy(names[j], t); }
    for (int i = 0; i < nn; i++) {
        size_t L = strlen(names[i]);
        if (L > 4 && !strcasecmp(names[i] + L - 4, ".bin")) { char base[96]; strcpy(base, names[i]); base[L - 4] = 0; int dup = 0; for (int j = 0; j < nn; j++) if (!strcasecmp(names[j], base)) dup = 1; if (dup) continue; }
        if (++c == index) { snprintf(out, 1100, "%s/%s", dir, names[i]); found = 1; break; }
    }
    return found;
}
static OF *getof(int r) { return (r > 0 && r < MAXOPEN && ofiles[r].f) ? &ofiles[r] : NULL; }
static int64_t posof(OF *o, uint32_t pb, int mode, int32_t off) {
    switch (mode & 3) { case 1: return off; case 2: return o->len + off; case 3: return o->mark + off; default: return o->mark; }
}
/* w = full trap word; n = normalised; returns OSErr */
static int fs_dispatch(uint16_t w, uint16_t n, uint32_t sel, uint32_t pb) {
    char path[1100], par[1100]; int isdir, e; fs_init();
    uint32_t dirid = (w & 0x200) ? rd32(pb + 48) : 0;
    if (!dirid) { int16_t v = (int16_t)rd16(pb + 22); if (v >= 0x100) dirid = v - 0x100; }
    switch (n) {
    case 0xA000: case 0xA00A: {
        e = resolve(dirid, rd32(pb + 18), path, &isdir); if (e) return e; if (isdir) return -43;
        int perm = rd8(pb + 27); int s = open_fork(path, n == 0xA00A, perm == 2 || perm == 3 || perm == 4); if (s < 0) return s; wr16(pb + 24, s); return 0; }
    case 0xA001: { OF *o = getof(rd16(pb + 24)); if (!o) return -51; fclose(o->f); o->f = NULL; return 0; }
    case 0xA002: {
        OF *o = getof(rd16(pb + 24)); if (!o) return -51;
        int64_t pos = posof(o, pb, rd16(pb + 44), (int32_t)rd32(pb + 46)); if (pos < 0) return -40;
        uint32_t req = rd32(pb + 36), buf = rd32(pb + 32); int64_t avail = o->len - pos; if (avail < 0) avail = 0;
        uint32_t cnt = req < avail ? req : (uint32_t)avail;
        fseek(o->f, o->base + pos, SEEK_SET); size_t got = fread(ram + (buf & 0xFFFFFF), 1, cnt, o->f); o->mark = pos + got;
        wr32(pb + 40, got); wr32(pb + 46, o->mark); return got < req ? -39 : 0; }
    case 0xA003: {
        OF *o = getof(rd16(pb + 24)); if (!o) return -51; if (o->rf || o->base) return -61;
        int64_t pos = posof(o, pb, rd16(pb + 44), (int32_t)rd32(pb + 46)); if (pos < 0) return -40;
        uint32_t req = rd32(pb + 36), buf = rd32(pb + 32);
        fseek(o->f, pos, SEEK_SET); size_t put = fwrite(ram + (buf & 0xFFFFFF), 1, req, o->f); fflush(o->f);
        o->mark = pos + put; if (o->mark > o->len) o->len = o->mark; wr32(pb + 40, put); wr32(pb + 46, o->mark); return put < req ? -34 : 0; }
    case 0xA011: { OF *o = getof(rd16(pb + 24)); if (!o) return -51; wr32(pb + 28, o->len); return 0; }
    case 0xA012: return 0;
    case 0xA018: { OF *o = getof(rd16(pb + 24)); if (!o) return -51; wr32(pb + 46, o->mark); return 0; }
    case 0xA044: { OF *o = getof(rd16(pb + 24)); if (!o) return -51; int64_t p = posof(o, pb, rd16(pb + 44), (int32_t)rd32(pb + 46)); if (p < 0 || p > o->len) return -40; o->mark = p; wr32(pb + 46, p); return 0; }
    case 0xA014: wr16(pb + 22, (uint16_t)cur_vref); if (rd32(pb + 18)) put_pstr(rd32(pb + 18), volname);
        if (w & 0x200) { wr32(pb + 48, cur_dir); wr16(pb + 22, 0x100 + cur_dir); } return 0;                       /* GetVol / HGetVol */
    case 0xA015: { uint32_t d = (w & 0x200) ? rd32(pb + 48) : 0; if (!d) { int16_t v = (int16_t)rd16(pb + 22); if (v >= 0x100) d = v - 0x100; } if (d) cur_dir = d; return 0; } /* SetVol / HSetVol */
    case 0xA00C: {                                                                                                 /* GetFInfo / HGetFInfo */
        int16_t idx = (int16_t)rd16(pb + 28);
        if (idx > 0) { const char *base = dir_path(dirid ? dirid : cur_dir); if (!base || !enum_dir(base, idx, path)) return -43; struct stat st; stat(path, &st); isdir = S_ISDIR(st.st_mode); }
        else { e = resolve(dirid, rd32(pb + 18), path, &isdir); if (e) return e; }
        dir_of_path(path, par); fill_cat(pb, path, isdir, dir_id_for(par)); return 0; }
    case 0xA008: {                                   /* Create */
        char nm[300]; pstr(rd32(pb + 18), nm, sizeof nm); const char *base = dir_path(dirid ? dirid : cur_dir); if (!base) return -120;
        char full[1100], parent[1100]; strncpy(parent, base, sizeof parent); char *leaf = nm;
        if (strchr(nm, ':')) { char *c = strrchr(nm, ':'); leaf = c + 1; if (c != nm) { *c = 0; char pp[1100]; int di; /* resolve prefix */ uint32_t t = rd32(pb + 18); (void)t;
            char tmpn[300]; tmpn[0] = strlen(nm); memcpy(tmpn + 1, nm, strlen(nm)); /* resolve via scratch pascal string */ uint32_t sa = 0x3F00; wr8(sa, tmpn[0]); for (int i = 0; i < tmpn[0]; i++) wr8(sa + 1 + i, nm[i]);
            if (resolve(dirid, sa, pp, &di) || !di) return -120; strncpy(parent, pp, sizeof parent); } }
        snprintf(full, sizeof full, "%s/%s", parent, leaf); struct stat st; if (!stat(full, &st)) return -48;
        FILE *f = fopen(full, "wb"); if (!f) return -61; fclose(f); return 0; }
    case 0xA009: {                                   /* Delete */
        e = resolve(dirid, rd32(pb + 18), path, &isdir); if (e) return e; remove(path); size_t L = strlen(path); if (!(L > 4 && !strcasecmp(path + L - 4, ".bin"))) { strcat(path, ".bin"); remove(path); } return 0; }
    case 0xA00D: { e = resolve(dirid, rd32(pb + 18), path, &isdir); return e; }   /* SetFileInfo: accepted, finder info not stored */
    case 0xA060:
        switch (sel & 0xFFFF) {
        case 1: { uint32_t d = rd32(pb + 48); if (!d) d = dirid ? dirid : cur_dir;                                   /* OpenWD */
            if (rd32(pb + 18) && rd8(rd32(pb + 18))) { e = resolve(d, rd32(pb + 18), path, &isdir); if (e) return e; if (!isdir) return -120; d = dir_id_for(path); }
            wr16(pb + 22, 0x100 + d); return 0; }
        case 2: return 0;
        case 7: { int16_t v = (int16_t)rd16(pb + 22); uint32_t d = v >= 0x100 ? (uint32_t)(v - 0x100) : cur_dir; wr16(pb + 32, (uint16_t)-1); wr32(pb + 48, d); if (rd32(pb + 18)) put_pstr(rd32(pb + 18), volname); return 0; }
        case 8: {                                                                                                  /* GetFCBInfo */
            int r = rd16(pb + 24); const char *nm = NULL; uint32_t parid = 2; long eof = 0, mark = 0;
            if (r == 1) { nm = "Castles - Siege & Conquest"; eof = 3216178; }
            else { OF *o = getof(r); if (!o) return -51; const char *b = strrchr(o->path, '/'); nm = b ? b + 1 : o->path; eof = o->len; mark = o->mark;
                   dir_of_path(o->path, par); parid = dir_id_for(par); }
            if (rd32(pb + 18)) put_pstr(rd32(pb + 18), nm);
            wr16(pb + 36, 0); wr32(pb + 40, eof); wr32(pb + 44, eof); wr32(pb + 48, mark); wr16(pb + 52, (uint16_t)-1); wr32(pb + 58, parid); return 0; }
        case 9: {                                                                                                  /* GetCatInfo */
            int16_t idx = (int16_t)rd16(pb + 28);
            if (idx > 0) { const char *base = dir_path(dirid ? dirid : cur_dir); if (!base || !enum_dir(base, idx, path)) return -43; struct stat st; stat(path, &st); isdir = S_ISDIR(st.st_mode); }
            else if (idx == -1) { const char *dp = dir_path(dirid ? dirid : cur_dir); if (!dp) return -120; strncpy(path, dp, 1100); isdir = 1; }
            else { e = resolve(dirid, rd32(pb + 18), path, &isdir); if (e) return e; }
            dir_of_path(path, par); fill_cat(pb, path, isdir, dir_id_for(par)); return 0; }
        default: fprintf(stderr, "  FSDispatch selector %u not implemented\n", sel & 0xFFFF); return -50; }
    default: return -1;
    }
}
