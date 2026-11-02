/* macbin: combine a data fork and a resource fork into one MacBinary (.bin) file.
 * usage: macbin <data file> <output.bin> [AppleDouble file | resource fork file]
 * The third argument may be an AppleDouble file (._name, as written by unar/The Unarchiver on Linux)
 * or a plain resource-fork file (name.rsrc). Used by install.sh when it unpacks a Mac archive. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
static uint8_t *slurp(const char *p, long *n) { FILE *f = fopen(p, "rb"); if (!f) { *n = -1; return NULL; } fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *b = malloc(*n + 1); if (fread(b, 1, *n, f) != (size_t)*n) { fclose(f); free(b); *n = -1; return NULL; } fclose(f); return b; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static uint16_t crc16(const uint8_t *d, int n) { uint16_t c = 0; for (int i = 0; i < n; i++) { c ^= (uint16_t)d[i] << 8; for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1; } return c; }
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: macbin <data file> <output.bin> [AppleDouble | .rsrc file]\n"); return 2; }
    long dl = 0, rl = 0, al = 0; uint8_t *data = slurp(argv[1], &dl); if (dl < 0) { data = NULL; dl = 0; }
    uint8_t *rsrc = NULL, *fi = NULL; (void)fi; uint8_t finfo[32]; memset(finfo, 0, 32);
    if (argc > 3) {
        uint8_t *a = slurp(argv[3], &al);
        if (al >= 26 && be32(a) == 0x00051607) {                       /* AppleDouble */
            int n = (a[24] << 8) | a[25];
            for (int i = 0; i < n && 26 + i * 12 + 12 <= al; i++) {
                uint32_t id = be32(a + 26 + i * 12), off = be32(a + 30 + i * 12), len = be32(a + 34 + i * 12);
                if ((long)off + len > al) continue;
                if (id == 2) { rsrc = malloc(len + 1); memcpy(rsrc, a + off, len); rl = len; }
                if (id == 9 && len >= 32) memcpy(finfo, a + off, 32);
            }
        } else if (al > 0) { rsrc = a; rl = al; }                       /* plain resource fork */
    }
    uint8_t h[128]; memset(h, 0, 128); const char *nm = strrchr(argv[1], '/'); nm = nm ? nm + 1 : argv[1];
    int nl = strlen(nm); if (nl > 63) nl = 63; h[1] = nl; memcpy(h + 2, nm, nl);
    memcpy(h + 65, finfo, 8); h[73] = finfo[8]; h[101] = finfo[9];       /* type, creator, Finder flags (hi/lo) */
    put32(h + 83, dl); put32(h + 87, rl);
    uint32_t t = (uint32_t)time(NULL) + 2082844800u; put32(h + 91, t); put32(h + 95, t);
    h[122] = 129; h[123] = 129;                                          /* MacBinary III / II compatible */
    uint16_t c = crc16(h, 124); h[124] = c >> 8; h[125] = c;
    FILE *o = fopen(argv[2], "wb"); if (!o) { perror(argv[2]); return 1; }
    fwrite(h, 1, 128, o);
    if (dl) fwrite(data, 1, dl, o);
    for (long i = 0, pad = (128 - dl % 128) % 128; i < pad; i++) fputc(0, o);
    if (rl) fwrite(rsrc, 1, rl, o);
    for (long i = 0, pad = (128 - rl % 128) % 128; i < pad; i++) fputc(0, o);
    fclose(o); return 0;
}
