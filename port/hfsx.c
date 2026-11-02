/* hfsx: minimal read-only extractor for classic Mac HFS floppy images (raw or Disk Copy 4.2).
 * usage: hfsx <image> <outdir>
 * Writes every file as <outdir>/<path>/<name> (data fork) and <name>.rsrc (resource fork, if any). Used by install.sh
 * to open the original floppy-disk images of the game's installer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>
static uint8_t *D; static long DN; static uint32_t ABS, ABST; /* allocation block size, first allocation block (in 512 byte sectors) */
static uint16_t be16(const uint8_t *p) { return p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
typedef struct { uint16_t s, c; } Ext;
static uint8_t *read_ext(const Ext *e, int n, uint32_t size, uint32_t *got) {
    uint8_t *out = calloc(size + ABS + 1, 1); uint32_t pos = 0;
    for (int i = 0; i < n && pos < size; i++) for (int k = 0; k < e[i].c && pos < size; k++) {
        long o = (long)ABST * 512 + (long)(e[i].s + k) * ABS; if (o + (long)ABS > DN) break;
        memcpy(out + pos, D + o, ABS); pos += ABS; }
    *got = pos < size ? pos : size; return out;
}
static void exts(const uint8_t *p, Ext *e) { for (int i = 0; i < 3; i++) { e[i].s = be16(p + i * 4); e[i].c = be16(p + i * 4 + 2); } }
typedef struct { uint8_t *b; uint32_t n; } Buf;
static Buf cat, ovf;
typedef struct { uint32_t fnum; uint8_t fk; uint16_t st; Ext e[3]; } Ov; static Ov *ov; static int nov;
/* iterate leaf records of a B-tree: callback(key, rec) */
static void leafs(Buf t, void (*cb)(const uint8_t *key, const uint8_t *rec)) {
    if (t.n < 32) return; uint32_t n = be32(t.b + 24); int guard = 0;
    while (n && guard++ < 100000) {
        if ((n + 1) * 512 > t.n) break; const uint8_t *nd = t.b + n * 512; int nrec = be16(nd + 10);
        for (int i = 0; i < nrec; i++) {
            int off = be16(nd + 512 - 2 * (i + 1)); int ln = nd[off]; if (!ln) continue;
            int rec = off + 1 + ln; if (rec & 1) rec++; if (rec >= 512) continue; cb(nd + off, nd + rec);
        }
        n = be32(nd);
    }
}
static void ov_cb(const uint8_t *key, const uint8_t *rec) { ov = realloc(ov, (nov + 1) * sizeof(Ov)); Ov *o = &ov[nov++]; o->fk = key[1]; o->fnum = be32(key + 2); o->st = be16(key + 6); exts(rec, o->e); }
static uint8_t *read_fork(uint32_t fnum, uint8_t fk, const uint8_t *first, uint32_t size, uint32_t *got) {
    Ext all[3 + 3 * 64]; int n = 0; Ext e0[3]; exts(first, e0); uint32_t have = 0; for (int i = 0; i < 3; i++) { all[n++] = e0[i]; have += e0[i].c; }
    if (size > have * ABS) { /* overflow extents, in start-block order */
        for (int pass = 0; pass < 64; pass++) { int best = -1; for (int i = 0; i < nov; i++) if (ov[i].fnum == fnum && ov[i].fk == fk && ov[i].st >= have && (best < 0 || ov[i].st < ov[best].st)) best = i;
            if (best < 0) break; for (int k = 0; k < 3; k++) { all[n++] = ov[best].e[k]; have += ov[best].e[k].c; } if (have * ABS >= size) break; }
    }
    return read_ext(all, n, size, got);
}
#define MAXD 4096
static uint32_t dirid[MAXD], dirpar[MAXD]; static char dirname_[MAXD][64]; static int ndir;
static const char *OUT; static int nfiles;
static void clean(const uint8_t *s, int n, char *out) { int j = 0; for (int i = 0; i < n && j < 62; i++) { uint8_t c = s[i]; if (c == '/') c = ':'; else if (c < 32) c = '_' ; else if (c >= 128) c = '_'; out[j++] = c; } out[j] = 0; }
static void dir_cb(const uint8_t *key, const uint8_t *rec) {
    if (rec[0] != 1 || ndir >= MAXD) return; clean(key + 7, key[6], dirname_[ndir]); dirid[ndir] = be32(rec + 6); dirpar[ndir] = be32(key + 2); ndir++;
}
static void path_of(uint32_t par, char *out) { /* build relative dir path for parent id */
    char parts[32][64]; int n = 0; while (par != 2 && n < 32) { int f = -1; for (int i = 0; i < ndir; i++) if (dirid[i] == par) { f = i; break; } if (f < 0) break; strcpy(parts[n++], dirname_[f]); par = dirpar[f]; }
    strcpy(out, OUT); for (int i = n - 1; i >= 0; i--) { strcat(out, "/"); strcat(out, parts[i]); }
}
static void mk(const char *p) { char t[2048]; snprintf(t, sizeof t, "%s", p); for (char *s = t + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(t, 0777); *s = '/'; } mkdir(t, 0777); }
static void file_cb(const uint8_t *key, const uint8_t *rec) {
    if (rec[0] != 2) return; char nm[64], dir[2048], path[3000]; clean(key + 7, key[6], nm); path_of(be32(key + 2), dir); mk(dir);
    uint32_t fnum = be32(rec + 20), dsz = be32(rec + 26), rsz = be32(rec + 36), got;
    uint8_t *d = read_fork(fnum, 0, rec + 74, dsz, &got); snprintf(path, sizeof path, "%s/%s", dir, nm); FILE *f = fopen(path, "wb"); if (f) { fwrite(d, 1, got, f); fclose(f); } free(d);
    if (rsz) { uint8_t *r = read_fork(fnum, 0xFF, rec + 86, rsz, &got); strcat(path, ".rsrc"); f = fopen(path, "wb"); if (f) { fwrite(r, 1, got, f); fclose(f); } free(r); }
    nfiles++; printf("%s data=%u rsrc=%u\n", nm, dsz, rsz);
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: hfsx <image> <outdir>\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; } fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *all = malloc(n); if (fread(all, 1, n, f) != (size_t)n) return 1; fclose(f);
    long off = 0; if (n > 84 && all[0] <= 63 && be16(all + 82) == 0x0100) off = 84;    /* Disk Copy 4.2 header */
    D = all + off; DN = n - off; if (DN < 2048 || be16(D + 1024) != 0x4244) { fprintf(stderr, "%s: not an HFS image\n", argv[1]); return 1; }
    const uint8_t *m = D + 1024; ABS = be32(m + 20); ABST = be16(m + 28); OUT = argv[2];
    Ext ce[3], oe[3]; exts(m + 150, ce); exts(m + 134, oe); uint32_t got;
    cat.b = read_ext(ce, 3, be32(m + 146), &got); cat.n = got; ovf.b = read_ext(oe, 3, be32(m + 130), &got); ovf.n = got;
    leafs(ovf, ov_cb); leafs(cat, dir_cb); mk(OUT); leafs(cat, file_cb);
    fprintf(stderr, "%s: volume \"%.*s\", %d files\n", argv[1], m[36], m + 37, nfiles); return 0;
}
