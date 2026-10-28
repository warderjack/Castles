/* ---- Palette Manager (device CLUT is the screen palette) ---- */
typedef struct { uint32_t w, pal; int updates; } PalAssoc; static PalAssoc pal_assoc[64]; static int npal_assoc = 0;
static uint32_t pal_of(uint32_t w) { for (int i = 0; i < npal_assoc; i++) if (pal_assoc[i].w == w) return pal_assoc[i].pal; return 0; }
static int pal_entries(uint32_t ph) { return ph && rd32(ph) ? rd16(rd32(ph)) : 0; }
static RGB pal_color(uint32_t ph, int i) { RGB c = {0, 0, 0}; if (i < 0 || i >= pal_entries(ph)) return c; uint32_t e = rd32(ph) + 16 + i * 24; c.r = rd16(e); c.g = rd16(e + 2); c.b = rd16(e + 4); return c; }
static void activate_palette(uint32_t ph) {
    int n = pal_entries(ph); if (!n) return; uint32_t ct = rd32(main_ct);
    uint8_t used[256]; memset(used, 0, sizeof used); used[0] = 1; used[255] = 1; int next = 1;
    for (int i = 0; i < n; i++) {
        RGB c = pal_color(ph, i); int usage = rd16(rd32(ph) + 16 + i * 24 + 6); int found = -1;
        if ((usage & 8) && i > 0 && i < 255) { found = i; }                                  /* pmExplicit: same device index */
        else for (int j = 0; j < 256; j++) { RGB d = ct_entry(main_ct, j); if (d.r == c.r && d.g == c.g && d.b == c.b) { found = j; break; } }
        if (found < 0) { while (next < 255 && used[next]) next++; if (next >= 255) { found = rgb_px(&(Surf){.depth = 8, .ctab = main_ct}, c); } else found = next; }
        if (found > 0 && found < 255) { used[found] = 1; uint32_t e = ct + 8 + found * 8; wr16(e + 2, c.r); wr16(e + 4, c.g); wr16(e + 6, c.b); }
    }
    wr32(ct, ++ctseed); gfx_dirty = 1;
}
static int pal_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    uint32_t sp = SP;
    switch (n) {
    case 0xAA90: return 1;                                                                                      /* InitPalettes */
    case 0xAA91: {      /* NewPalette(entries, srcColors, usage, tolerance) */
        int tol = (int16_t)rd16(sp), usage = (int16_t)rd16(sp + 2); uint32_t src = rd32(sp + 4); int ent = (int16_t)rd16(sp + 8); setsp(sp + 10);
        uint32_t ph = new_handle(16 + 24 * (ent > 0 ? ent : 1)); uint32_t a = rd32(ph); wr16(a, ent);
        for (int i = 0; i < ent; i++) { uint32_t e = a + 16 + i * 24; RGB c = {0, 0, 0}; if (src && rd32(src)) c = ct_entry(src, i); wr16(e, c.r); wr16(e + 2, c.g); wr16(e + 4, c.b); wr16(e + 6, usage); wr16(e + 8, tol); }
        wr32(sp + 10, ph); return 1; }
    case 0xAA92: { setsp(sp + 2); wr32(sp + 2, 0); return 1; }                                                  /* GetNewPalette (no pltt resources) */
    case 0xAA93: { uint32_t ph = rd32(sp); setsp(sp + 4); dispose_handle(ph); return 1; }
    case 0xAA94: { uint32_t wn = rd32(sp); setsp(sp + 4); uint32_t ph = pal_of(wn); if (ph) activate_palette(ph); return 1; }
    case 0xAA95: {      /* SetPalette(window, palette, cUpdates) */
        uint32_t ph = rd32(sp + 2), wn = rd32(sp + 6); setsp(sp + 10); int i; for (i = 0; i < npal_assoc; i++) if (pal_assoc[i].w == wn) break;
        if (i == npal_assoc) { if (npal_assoc >= 64) return 1; npal_assoc++; } pal_assoc[i].w = wn; pal_assoc[i].pal = ph; return 1; }
    case 0xAA96: { uint32_t wn = rd32(sp); setsp(sp + 4); wr32(sp + 4, pal_of(wn)); return 1; }
    case 0xAA97: case 0xAA98: {      /* PmForeColor / PmBackColor(dstEntry) */
        int e = (int16_t)rd16(sp); setsp(sp + 2); uint32_t port = the_port(); uint32_t ph = port ? pal_of(port) : 0; if (ph && port_is_color(port)) { RGB c = pal_color(ph, e); if (n == 0xAA97) set_fore_rgb(port, c); else set_back_rgb(port, c); } return 1; }
    case 0xAA99: { setsp(sp + 10); return 1; }                                                                  /* AnimateEntry */
    case 0xAA9A: { setsp(sp + 14); return 1; }                                                                  /* AnimatePalette */
    case 0xAA9B: { uint32_t p = rd32(sp); int e = (int16_t)rd16(sp + 4); uint32_t ph = rd32(sp + 6); setsp(sp + 10); RGB c = pal_color(ph, e); wr16(p, c.r); wr16(p + 2, c.g); wr16(p + 4, c.b); return 1; }
    case 0xAA9C: { uint32_t p = rd32(sp); int e = (int16_t)rd16(sp + 4); uint32_t ph = rd32(sp + 6); setsp(sp + 10); if (e >= 0 && e < pal_entries(ph)) { uint32_t a = rd32(ph) + 16 + e * 24; wr16(a, rd16(p)); wr16(a + 2, rd16(p + 2)); wr16(a + 4, rd16(p + 4)); } return 1; }
    case 0xAA9D: { uint32_t tp = rd32(sp), up = rd32(sp + 4); int e = (int16_t)rd16(sp + 8); uint32_t ph = rd32(sp + 10); setsp(sp + 14); if (e >= 0 && e < pal_entries(ph)) { uint32_t a = rd32(ph) + 16 + e * 24; wr16(up, rd16(a + 6)); wr16(tp, rd16(a + 8)); } return 1; }
    case 0xAA9E: { int tol = (int16_t)rd16(sp), us = (int16_t)rd16(sp + 2); int e = (int16_t)rd16(sp + 4); uint32_t ph = rd32(sp + 6); setsp(sp + 10); if (e >= 0 && e < pal_entries(ph)) { uint32_t a = rd32(ph) + 16 + e * 24; wr16(a + 6, us); wr16(a + 8, tol); } return 1; }
    case 0xAA9F: { setsp(sp + 12); return 1; }                                                                  /* CTab2Palette */
    case 0xAAA0: { uint32_t ct = rd32(sp), ph = rd32(sp + 4); setsp(sp + 8); int ne = pal_entries(ph); if (ct && ne) { set_handle_size(ct, 8 + 8 * ne); uint32_t c = rd32(ct); wr16(c + 6, ne - 1); for (int i = 0; i < ne; i++) { RGB k = pal_color(ph, i); wr16(c + 8 + i * 8, i); wr16(c + 10 + i * 8, k.r); wr16(c + 12 + i * 8, k.g); wr16(c + 14 + i * 8, k.b); } } return 1; }
    case 0xAAA1: { setsp(sp + 8); return 1; }                                                                   /* CopyPalette */
    default: return 0;
    }
}
