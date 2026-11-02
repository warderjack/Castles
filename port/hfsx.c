/* hfsx: minimal read-only extractor for classic Mac HFS floppy images (raw or Disk Copy 4.2).
 * usage: hfsx <image> <outdir>   (image: Disk Copy 4.2, raw HFS floppy, .iso or raw .bin CD with a partition map)
 * env: HFSX_LIST=1 only lists files (full path, sizes); HFSX_FILTER='prefix|prefix' only handles files whose path starts with one of the prefixes (a prefix ending in $ must match the whole path).
 * Writes every file as <outdir>/<path>/<name> (data fork) and <name>.rsrc (resource fork, if any). Used by install.sh
 * to open the original floppy-disk images of the game's installer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>
#include <strings.h>
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
    if (rec[0] != 2) return; char nm[64], dir[2048], path[3000]; clean(key + 7, key[6], nm); path_of(be32(key + 2), dir);
    uint32_t fnum = be32(rec + 20), dsz = be32(rec + 26), rsz = be32(rec + 36), got;
    { const char *rel = dir + strlen(OUT); if (*rel == '/') rel++; const char *fl = getenv("HFSX_FILTER"); char full[3100]; snprintf(full, sizeof full, "%s%s%s", rel, *rel ? "/" : "", nm);
      if (fl && *fl) { char tmp[2048]; snprintf(tmp, sizeof tmp, "%s", fl); int hit = 0; for (char *t = strtok(tmp, "|"); t; t = strtok(NULL, "|")) { size_t tl = strlen(t); if (tl && t[tl - 1] == '$') { if (strlen(full) == tl - 1 && !strncasecmp(full, t, tl - 1)) hit = 1; } else if (!strncasecmp(full, t, tl)) hit = 1; } if (!hit) return; }
      if (getenv("HFSX_LIST")) { printf("%s data=%u rsrc=%u\n", full, dsz, rsz); nfiles++; return; } }
    mk(dir);
    uint8_t *d = read_fork(fnum, 0, rec + 74, dsz, &got); snprintf(path, sizeof path, "%s/%s", dir, nm); FILE *f = fopen(path, "wb"); if (f) { fwrite(d, 1, got, f); fclose(f); } free(d);
    if (rsz) { uint8_t *r = read_fork(fnum, 0xFF, rec + 86, rsz, &got); strcat(path, ".rsrc"); f = fopen(path, "wb"); if (f) { fwrite(r, 1, got, f); fclose(f); } free(r); }
    nfiles++; printf("%s data=%u rsrc=%u\n", nm, dsz, rsz);
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: hfsx <image> <outdir>\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; } fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t head[4096]; size_t hn = fread(head, 1, sizeof head, f); if (hn < 2048) { fprintf(stderr, "%s: too small\n", argv[1]); return 1; }
    /* sector layout: raw CD (12-byte sync, 2352-byte sectors, MODE1 data at +16 / MODE2 at +24) or plain 2048/512-byte images */
    static const uint8_t sync[12] = {0,255,255,255,255,255,255,255,255,255,255,0};
    long ss = 2048, dofs = 0; if (hn >= 2352 && !memcmp(head, sync, 12)) { ss = 2352; dofs = head[15] == 2 ? 24 : 16; }
    long nsec = n / ss; long loff = 0;   /* logical bytes are read through rd(): the data track only (stops at the first non-sync sector) */
    if (ss == 2352) { uint8_t sh[16]; long k; for (k = 0; k < nsec; k++) { fseek(f, k * ss, SEEK_SET); if (fread(sh, 1, 16, f) != 16 || memcmp(sh, sync, 12)) break; } nsec = k; }
    long dn = nsec * 2048; if (ss == 2048 && (n % 2048)) dn = n;   /* odd sizes (floppy images): keep every byte */
    if (ss == 2048 && (hn > 84 && head[0] <= 63 && be16(head + 82) == 0x0100)) { ss = 1; dn = n; }   /* Disk Copy 4.2: handled below as a flat file */
    uint8_t *all = NULL; (void)loff;
    if (ss == 1) { all = malloc(n); fseek(f, 0, SEEK_SET); if (fread(all, 1, n, f) != (size_t)n) return 1; D = all + 84; DN = n - 84; }
    else {
        /* read the first sectors to find the partition map (Apple Partition Map: 'ER' block 0, 'PM' entries from block 1; blocks are 512 bytes) */
        #define RDLOG(buf, lsec, cnt) do { for (long q_ = 0; q_ < (cnt); q_++) { fseek(f, (long)((lsec) + q_) * ss + dofs, SEEK_SET); if (fread((buf) + q_ * 2048, 1, 2048, f) != 2048) memset((buf) + q_ * 2048, 0, 2048); } } while (0)
        uint8_t *hd = malloc(2048 * 64); RDLOG(hd, 0, 64 < nsec ? 64 : nsec);
        long pstart = 0, plen = dn / 512; int found = 0; const uint8_t *pm = hd + 512;
        if (hd[0] == 'E' && hd[1] == 'R' && pm[0] == 'P' && pm[1] == 'M') {
            long cnt = be32(pm + 4); if (cnt > 60) cnt = 60; long best = 0;
            for (long i = 0; i < cnt; i++) { const uint8_t *e = hd + 512 * (1 + i); if (e[0] != 'P' || e[1] != 'M') break;
                fprintf(stderr, "partition %ld: %.32s  type %.32s  start %u  blocks %u\n", i + 1, e + 16, e + 48, be32(e + 8), be32(e + 12));
                if (!strncmp((const char *)e + 48, "Apple_HFS", 9) && be32(e + 12) > best) { best = be32(e + 12); pstart = be32(e + 8); plen = best; found = 1; } }
            if (!found) { fprintf(stderr, "%s: partition map has no Apple_HFS partition\n", argv[1]); return 1; }
        }
        DN = plen * 512; if (pstart * 512 + DN > dn) DN = dn - pstart * 512;
        D = malloc(DN); long first = (pstart * 512) / 2048, skip = (pstart * 512) % 2048; long nsc = (skip + DN + 2047) / 2048; uint8_t *tmp = malloc(2048 * 256);
        for (long done = 0; done < nsc; done += 256) { long c = nsc - done < 256 ? nsc - done : 256; RDLOG(tmp, first + done, c);
            long from = done == 0 ? skip : 0, dst = done * 2048 - skip + from; long len = c * 2048 - from; if (dst + len > DN) len = DN - dst; if (len > 0) memcpy(D + dst, tmp + from, len); }
        free(tmp); free(hd);
    }
    fclose(f);
    if (DN < 2048 || be16(D + 1024) != 0x4244) { fprintf(stderr, "%s: not a classic HFS volume%s\n", argv[1], (DN >= 2048 && be16(D + 1024) == 0x482B) ? " (it is HFS+, which hfsx cannot read)" : ""); return 1; }
    if (be16(D + 1024 + 124) == 0x482B) { fprintf(stderr, "%s: HFS+ volume wrapped in HFS (hfsx cannot read HFS+)\n", argv[1]); return 1; }
    const uint8_t *m = D + 1024; ABS = be32(m + 20); ABST = be16(m + 28); OUT = argv[2];
    Ext ce[3], oe[3]; exts(m + 150, ce); exts(m + 134, oe); uint32_t got;
    cat.b = read_ext(ce, 3, be32(m + 146), &got); cat.n = got; ovf.b = read_ext(oe, 3, be32(m + 130), &got); ovf.n = got;
    leafs(ovf, ov_cb); leafs(cat, dir_cb); mk(OUT); leafs(cat, file_cb);
    fprintf(stderr, "%s: volume \"%.*s\", %d files\n", argv[1], m[36], m + 37, nfiles); return 0;
}
