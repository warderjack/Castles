/* ---- SANE: FP68K (A9EB), Elems68K (A9EC), Pack7 numeric conversions (A9EE) using host long double ---- */
#include <math.h>
#include <float.h>
static long double x80_get(uint32_t a) {
    uint16_t se = rd16(a); uint64_t m = ((uint64_t)rd32(a + 2) << 32) | rd32(a + 6);
    union { long double ld; struct { uint64_t m; uint16_t se; } p; } u; memset(&u, 0, sizeof u); u.p.m = m; u.p.se = se; return u.ld;
}
static void x80_put(uint32_t a, long double v) {
    union { long double ld; struct { uint64_t m; uint16_t se; } p; } u; memset(&u, 0, sizeof u); u.ld = v;
    wr16(a, u.p.se); wr32(a + 2, (uint32_t)(u.p.m >> 32)); wr32(a + 6, (uint32_t)u.p.m); wr16(a + 10, 0);
}
static long double sane_get(uint32_t a, int fmt) {
    switch (fmt) {
    case 0x0800: { uint64_t b = ((uint64_t)rd32(a) << 32) | rd32(a + 4); double d; memcpy(&d, &b, 8); return d; }
    case 0x1000: { uint32_t b = rd32(a); float f; memcpy(&f, &b, 4); return f; }
    case 0x2000: return (int16_t)rd16(a);
    case 0x2800: return (int32_t)rd32(a);
    case 0x3000: { int64_t c = (int64_t)(((uint64_t)rd32(a) << 32) | rd32(a + 4)); if (c == INT64_MIN) return NAN; return (long double)c; }
    default: return x80_get(a);
    }
}
static void sane_put(uint32_t a, int fmt, long double v) {
    switch (fmt) {
    case 0x0800: { double d = (double)v; uint64_t b; memcpy(&b, &d, 8); wr32(a, b >> 32); wr32(a + 4, (uint32_t)b); break; }
    case 0x1000: { float f = (float)v; uint32_t b; memcpy(&b, &f, 4); wr32(a, b); break; }
    case 0x2000: { long double r = rintl(v); wr16(a, isnan(r) || r > 32767 || r < -32768 ? 0x8000 : (uint16_t)(int16_t)r); break; }
    case 0x2800: { long double r = rintl(v); wr32(a, isnan(r) || r > 2147483647.0L || r < -2147483648.0L ? 0x80000000u : (uint32_t)(int32_t)r); break; }
    case 0x3000: { long double r = rintl(v); int64_t c = isnan(r) || fabsl(r) >= 9.2233720368547758e18L ? INT64_MIN : (int64_t)r; wr32(a, (uint64_t)c >> 32); wr32(a + 4, (uint32_t)c); break; }
    default: x80_put(a, v); break;
    }
}
static void set_ccr_bits(int x, int n, int z, int v, int c) { uint32_t sr = R(M68K_REG_SR) & ~0x1Fu; sr |= (x ? 0x10 : 0) | (n ? 8 : 0) | (z ? 4 : 0) | (v ? 2 : 0) | (c ? 1 : 0); W(M68K_REG_SR, sr); }
static uint16_t sane_env = 0;
static void dec_from_ld(uint32_t df, uint32_t x, int fmt, uint32_t dec) {
    long double v = sane_get(x, fmt); int style = rd16(df), digits = (int16_t)rd16(df + 2); char buf[128];
    int sgn = signbit(v) ? 1 : 0; wr16(dec, sgn); char sig[40]; int exp10 = 0;
    if (isnan(v)) { strcpy(sig, "N"); }
    else if (isinf(v)) { strcpy(sig, "I"); }
    else if (v == 0) { strcpy(sig, "0"); }
    else {
        long double a = fabsl(v);
        if (style == 0) { int d = digits < 1 ? 1 : (digits > 20 ? 20 : digits); snprintf(buf, sizeof buf, "%.*Le", d - 1, a); char *e = strchr(buf, 'e'); int ex = atoi(e + 1); int k = 0; for (char *p = buf; p < e; p++) if (*p >= '0' && *p <= '9') sig[k++] = *p; sig[k] = 0; exp10 = ex - (k - 1); }
        else { int d = digits < 0 ? 0 : (digits > 19 ? 19 : digits); snprintf(buf, sizeof buf, "%.*Lf", d, a); int k = 0; for (char *p = buf; *p; p++) if (*p >= '0' && *p <= '9') { if (k == 0 && *p == '0' && p[1] && p[1] != '.') continue; sig[k++] = *p; } sig[k] = 0; exp10 = -d;
            int lead = 0; while (sig[lead] == '0' && sig[lead + 1]) lead++; if (lead) memmove(sig, sig + lead, strlen(sig) - lead + 1); }
    }
    wr16(dec + 2, (uint16_t)exp10); int l = strlen(sig); wr8(dec + 4, l); for (int i = 0; i < l; i++) wr8(dec + 5 + i, sig[i]);
}
static long double ld_from_dec(uint32_t dec) {
    int sgn = rd16(dec) & 1, ex = (int16_t)rd16(dec + 2), l = rd8(dec + 4); char s[64]; for (int i = 0; i < l && i < 40; i++) s[i] = rd8(dec + 5 + i); s[l < 40 ? l : 40] = 0;
    long double v;
    if (s[0] == 'I') v = INFINITY; else if (s[0] == 'N') v = NAN; else { char t[128]; snprintf(t, sizeof t, "%se%d", s[0] ? s : "0", ex); v = strtold(t, NULL); }
    return sgn ? -v : v;
}
static int sane_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    uint32_t sp = SP;
    if (n == 0xA9EB) {
        uint32_t op = rd16(sp); int sel = op & 0xFF; int fmt = op & 0x3800;
        switch (sel) {
        case 0x00: case 0x02: case 0x04: case 0x06: {      /* add sub mul div: dst op= src */
            uint32_t dst = rd32(sp + 2), src = rd32(sp + 6); setsp(sp + 10); long double a = x80_get(dst), b = sane_get(src, fmt), r;
            r = sel == 0 ? a + b : sel == 2 ? a - b : sel == 4 ? a * b : a / b; x80_put(dst, r); return 1; }
        case 0x08: case 0x0A: {      /* cmp / cpx */
            uint32_t dst = rd32(sp + 2), src = rd32(sp + 6); setsp(sp + 10); long double a = x80_get(dst), b = sane_get(src, fmt);
            if (isnan(a) || isnan(b)) set_ccr_bits(0, 0, 0, 1, 0); else if (a == b) set_ccr_bits(0, 0, 1, 0, 0); else if (a < b) set_ccr_bits(1, 1, 0, 0, 1); else set_ccr_bits(0, 0, 0, 0, 0); return 1; }
        case 0x0C: { uint32_t dst = rd32(sp + 2), src = rd32(sp + 6); setsp(sp + 10); long double a = x80_get(dst), b = sane_get(src, fmt); x80_put(dst, remainderl(a, b)); return 1; }
        case 0x0D: { uint32_t dst = rd32(sp + 2); setsp(sp + 6); x80_put(dst, -x80_get(dst)); return 1; }
        case 0x0F: { uint32_t dst = rd32(sp + 2); setsp(sp + 6); x80_put(dst, fabsl(x80_get(dst))); return 1; }
        case 0x12: { uint32_t dst = rd32(sp + 2); setsp(sp + 6); x80_put(dst, sqrtl(x80_get(dst))); return 1; }
        case 0x14: { uint32_t dst = rd32(sp + 2); setsp(sp + 6); x80_put(dst, rintl(x80_get(dst))); return 1; }
        case 0x16: { uint32_t dst = rd32(sp + 2); setsp(sp + 6); x80_put(dst, truncl(x80_get(dst))); return 1; }
        case 0x1A: { uint32_t dst = rd32(sp + 2); setsp(sp + 6); long double v = x80_get(dst); x80_put(dst, v == 0 ? -INFINITY : isinf(v) ? INFINITY : logbl(v)); return 1; }
        case 0x18: { uint32_t dst = rd32(sp + 2), src = rd32(sp + 6); setsp(sp + 10); x80_put(dst, scalbnl(x80_get(dst), (int16_t)rd16(src))); return 1; }
        case 0x11: { uint32_t dst = rd32(sp + 2), src = rd32(sp + 6); setsp(sp + 10); x80_put(dst, copysignl(x80_get(dst), x80_get(src))); return 1; }
        case 0x0E: { uint32_t dst = rd32(sp + 2), src = rd32(sp + 6); setsp(sp + 10); x80_put(dst, sane_get(src, fmt)); return 1; }          /* x2X: any -> extended */
        case 0x10: { uint32_t dst = rd32(sp + 2), src = rd32(sp + 6); setsp(sp + 10); sane_put(dst, fmt, x80_get(src)); return 1; }          /* X2x: extended -> any */
        case 0x1C: {      /* classify */
            uint32_t dst = rd32(sp + 2), src = rd32(sp + 6); setsp(sp + 10); long double v = sane_get(src, fmt); int cls;
            if (isnan(v)) cls = 2; else if (isinf(v)) cls = 3; else if (v == 0) cls = 4; else if (fmt == 0 && fabsl(v) < LDBL_MIN) cls = 6; else cls = 5;
            wr16(dst, signbit(v) ? (uint16_t)-cls : cls); return 1; }
        case 0x09: { uint32_t dst = rd32(sp + 2), src = rd32(sp + 6); setsp(sp + 10); sane_put(dst, fmt, ld_from_dec(src)); return 1; }       /* dec2x */
        case 0x0B: { uint32_t dec = rd32(sp + 2), x = rd32(sp + 6), df = rd32(sp + 10); setsp(sp + 14); dec_from_ld(df, x, fmt, dec); return 1; }   /* x2dec */
        case 0x01: { uint32_t dp = rd32(sp + 2); setsp(sp + 6); sane_env = rd16(dp); return 1; }                                                    /* setenv */
        case 0x03: { uint32_t dp = rd32(sp + 2); setsp(sp + 6); wr16(dp, sane_env); return 1; }                                                     /* getenv */
        case 0x17: { uint32_t dp = rd32(sp + 2); setsp(sp + 6); wr16(dp, sane_env); sane_env = 0; return 1; }                                       /* procentry */
        case 0x19: { uint32_t dp = rd32(sp + 2); setsp(sp + 6); sane_env = rd16(dp); return 1; }                                                    /* procexit */
        case 0x1B: { uint32_t dp = rd32(sp + 2); setsp(sp + 6); wr16(dp, 0); return 1; }                                                            /* testxcp */
        default: fprintf(stderr, "  [sane] FP68K op %04x not implemented\n", op); return 0;
        }
    }
    if (n == 0xA9EC) {
        uint32_t op = rd16(sp); int sel = op & 0xFF; uint32_t dst = rd32(sp + 2); long double x;
        switch (sel) {
        case 0x00: case 0x02: case 0x04: case 0x06: case 0x08: case 0x0A: case 0x0C: case 0x0E: case 0x18: case 0x1A: case 0x1C: case 0x1E: {
            setsp(sp + 6); x = x80_get(dst); long double r;
            switch (sel) { case 0x00: r = logl(x); break; case 0x02: r = log2l(x); break; case 0x04: r = log1pl(x); break; case 0x06: r = log2l(1 + x); break;
            case 0x08: r = expl(x); break; case 0x0A: r = exp2l(x); break; case 0x0C: r = expm1l(x); break; case 0x0E: r = exp2l(x) - 1; break;
            case 0x18: r = sinl(x); break; case 0x1A: r = cosl(x); break; case 0x1C: r = tanl(x); break; default: r = atanl(x); break; }
            x80_put(dst, r); return 1; }
        case 0x10: { uint32_t src = rd32(sp + 6); setsp(sp + 10); x80_put(dst, powl(x80_get(dst), (int16_t)rd16(src))); return 1; }      /* x ^ i : here dst=? (src=int ptr first) */
        case 0x12: { uint32_t src = rd32(sp + 6); setsp(sp + 10); x80_put(dst, powl(x80_get(dst), x80_get(src))); return 1; }
        case 0x14: case 0x16: { uint32_t r = rd32(sp + 6), nn = rd32(sp + 10); setsp(sp + 14); long double rr = x80_get(r), N = x80_get(nn);
            x80_put(dst, sel == 0x14 ? powl(1 + rr, N) : (rr == 0 ? N : (1 - powl(1 + rr, -N)) / rr)); return 1; }
        case 0x20: { setsp(sp + 6); static uint64_t s = 1; s = s * 0x5DEECE66DULL + 0xB; x80_put(dst, (long double)(s >> 16 & 0x7FFFFFFF) / 2147483648.0L); return 1; }
        default: fprintf(stderr, "  [sane] Elems68K op %04x not implemented\n", op); return 0;
        }
    }
    if (n == 0xA9EE) {      /* Pack7 */
        int sel = rd16(sp);
        if (sel == 0) { int32_t v = (int32_t)rd32(sp + 6); uint32_t s = rd32(sp + 2); setsp(sp + 10); char b[20]; snprintf(b, sizeof b, "%d", v); wr8(s, strlen(b)); for (size_t i = 0; i < strlen(b); i++) wr8(s + 1 + i, b[i]); return 1; }
        if (sel == 1) { uint32_t outp = rd32(sp + 2), s = rd32(sp + 6); setsp(sp + 10); char b[300]; int l = rd8(s); for (int i = 0; i < l; i++) b[i] = rd8(s + 1 + i); b[l] = 0; wr32(outp, (uint32_t)atoi(b)); return 1; }
        fprintf(stderr, "  [sane] Pack7 selector %d not implemented\n", sel); return 0;
    }
    return 0;
}
