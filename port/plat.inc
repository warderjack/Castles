/* ---- Platform layer: SDL2 window/input/audio (optional), headless fallback with scripted input and screenshots ---- */
#ifdef HAVE_SDL
#include <SDL.h>
#endif

typedef struct { int type, x, y, key, chr, mods, btn; } PEvt;      /* type: 1 down 2 up 3 keydown 4 keyup 5 move 6 quit 7 autokey */
enum { PE_DOWN = 1, PE_UP, PE_KEY, PE_KEYUP, PE_MOVE, PE_QUIT, PE_AUTOKEY };
static int plat_mx = 320, plat_my = 240, plat_btn = 0, plat_mods = 0; static uint8_t keymap_state[16];
static int headless = 0, plat_scale = 2;
static uint32_t *plat_argb;                 /* converted frame */
static void plat_set_key(int mac, int down) { if (mac < 0 || mac > 127) return; if (down) keymap_state[mac >> 3] |= 1 << (mac & 7); else keymap_state[mac >> 3] &= ~(1 << (mac & 7)); }

#ifdef HAVE_SDL
static SDL_Window *sdl_win; static SDL_Renderer *sdl_ren; static SDL_Texture *sdl_tex;
static int sdl2mac(int sc) {
    switch (sc) {
    case SDL_SCANCODE_A: return 0x00; case SDL_SCANCODE_S: return 0x01; case SDL_SCANCODE_D: return 0x02; case SDL_SCANCODE_F: return 0x03; case SDL_SCANCODE_H: return 0x04;
    case SDL_SCANCODE_G: return 0x05; case SDL_SCANCODE_Z: return 0x06; case SDL_SCANCODE_X: return 0x07; case SDL_SCANCODE_C: return 0x08; case SDL_SCANCODE_V: return 0x09;
    case SDL_SCANCODE_B: return 0x0B; case SDL_SCANCODE_Q: return 0x0C; case SDL_SCANCODE_W: return 0x0D; case SDL_SCANCODE_E: return 0x0E; case SDL_SCANCODE_R: return 0x0F;
    case SDL_SCANCODE_Y: return 0x10; case SDL_SCANCODE_T: return 0x11; case SDL_SCANCODE_1: return 0x12; case SDL_SCANCODE_2: return 0x13; case SDL_SCANCODE_3: return 0x14;
    case SDL_SCANCODE_4: return 0x15; case SDL_SCANCODE_6: return 0x16; case SDL_SCANCODE_5: return 0x17; case SDL_SCANCODE_EQUALS: return 0x18; case SDL_SCANCODE_9: return 0x19;
    case SDL_SCANCODE_7: return 0x1A; case SDL_SCANCODE_MINUS: return 0x1B; case SDL_SCANCODE_8: return 0x1C; case SDL_SCANCODE_0: return 0x1D; case SDL_SCANCODE_RIGHTBRACKET: return 0x1E;
    case SDL_SCANCODE_O: return 0x1F; case SDL_SCANCODE_U: return 0x20; case SDL_SCANCODE_LEFTBRACKET: return 0x21; case SDL_SCANCODE_I: return 0x22; case SDL_SCANCODE_P: return 0x23;
    case SDL_SCANCODE_RETURN: return 0x24; case SDL_SCANCODE_L: return 0x25; case SDL_SCANCODE_J: return 0x26; case SDL_SCANCODE_APOSTROPHE: return 0x27; case SDL_SCANCODE_K: return 0x28;
    case SDL_SCANCODE_SEMICOLON: return 0x29; case SDL_SCANCODE_BACKSLASH: return 0x2A; case SDL_SCANCODE_COMMA: return 0x2B; case SDL_SCANCODE_SLASH: return 0x2C; case SDL_SCANCODE_N: return 0x2D;
    case SDL_SCANCODE_M: return 0x2E; case SDL_SCANCODE_PERIOD: return 0x2F; case SDL_SCANCODE_TAB: return 0x30; case SDL_SCANCODE_SPACE: return 0x31; case SDL_SCANCODE_GRAVE: return 0x32;
    case SDL_SCANCODE_BACKSPACE: return 0x33; case SDL_SCANCODE_ESCAPE: return 0x35; case SDL_SCANCODE_LEFT: return 0x7B; case SDL_SCANCODE_RIGHT: return 0x7C; case SDL_SCANCODE_DOWN: return 0x7D;
    case SDL_SCANCODE_UP: return 0x7E; case SDL_SCANCODE_KP_ENTER: return 0x4C; case SDL_SCANCODE_DELETE: return 0x75; case SDL_SCANCODE_HOME: return 0x73; case SDL_SCANCODE_END: return 0x77;
    case SDL_SCANCODE_PAGEUP: return 0x74; case SDL_SCANCODE_PAGEDOWN: return 0x79; case SDL_SCANCODE_F1: return 0x7A; case SDL_SCANCODE_F2: return 0x78; case SDL_SCANCODE_F3: return 0x63;
    case SDL_SCANCODE_F4: return 0x76; case SDL_SCANCODE_F5: return 0x60; case SDL_SCANCODE_F6: return 0x61; case SDL_SCANCODE_F7: return 0x62; case SDL_SCANCODE_F8: return 0x64;
    case SDL_SCANCODE_F9: return 0x65; case SDL_SCANCODE_F10: return 0x6D; case SDL_SCANCODE_F11: return 0x67; case SDL_SCANCODE_F12: return 0x6F;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return 0x38; case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x3B; case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x3A;
    case SDL_SCANCODE_LGUI: case SDL_SCANCODE_RGUI: return 0x37; case SDL_SCANCODE_CAPSLOCK: return 0x39;
    default: return -1;
    }
}
static int mac_char(SDL_Keysym ks) {
    int sym = ks.sym, shift = (ks.mod & KMOD_SHIFT) != 0;
    switch (sym) { case SDLK_RETURN: return 0x0D; case SDLK_KP_ENTER: return 0x03; case SDLK_TAB: return 9; case SDLK_BACKSPACE: return 8; case SDLK_ESCAPE: return 0x1B; case SDLK_DELETE: return 0x7F;
    case SDLK_LEFT: return 0x1C; case SDLK_RIGHT: return 0x1D; case SDLK_UP: return 0x1E; case SDLK_DOWN: return 0x1F; case SDLK_HOME: return 1; case SDLK_END: return 4; case SDLK_PAGEUP: return 0x0B; case SDLK_PAGEDOWN: return 0x0C; default: break; }
    if (sym >= 'a' && sym <= 'z') return shift ? sym - 32 : sym;
    if (sym >= 32 && sym < 127) { if (!shift) return sym; static const char *from = "1234567890-=[];'`\\,./", *to = "!@#$%^&*()_+{}:\"~|<>?"; const char *p = strchr(from, sym); return p ? to[p - from] : sym; }
    return 0;
}
#endif

static void plat_init(void) {
    plat_argb = malloc(SCR_W * SCR_H * 4);
    if (getenv("CASTLES_HEADLESS")) { headless = 1; return; }
#ifdef HAVE_SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) { fprintf(stderr, "SDL_Init failed: %s -- running headless\n", SDL_GetError()); headless = 1; return; }
    const char *sc = getenv("CASTLES_SCALE"); if (sc) plat_scale = atoi(sc); if (plat_scale < 1) plat_scale = 1;
    sdl_win = SDL_CreateWindow("Castles: Siege & Conquest", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCR_W * plat_scale, SCR_H * plat_scale, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    sdl_ren = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_ren) sdl_ren = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_SOFTWARE);
    SDL_RenderSetLogicalSize(sdl_ren, SCR_W, SCR_H);
    sdl_tex = SDL_CreateTexture(sdl_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
#else
    headless = 1;
#endif
}
static void fb_to_argb(void) {
    uint32_t pal[256]; uint32_t ct = main_ct ? rd32(main_ct) : 0;
    for (int i = 0; i < 256; i++) { RGB c = ct_entry(main_ct, i); (void)ct; pal[i] = 0xFF000000u | ((c.r >> 8) << 16) | ((c.g >> 8) << 8) | (c.b >> 8); }
    const uint8_t *fb = ram + FB_BASE; for (int i = 0; i < SCR_W * SCR_H; i++) plat_argb[i] = pal[fb[i]];
}
static void plat_present(void) {
    fb_to_argb();
#ifdef HAVE_SDL
    if (!headless && sdl_tex) { SDL_UpdateTexture(sdl_tex, NULL, plat_argb, SCR_W * 4); SDL_RenderClear(sdl_ren); SDL_RenderCopy(sdl_ren, sdl_tex, NULL, NULL); SDL_RenderPresent(sdl_ren); }
#endif
}
static void write_ppm(const char *path) {
    fb_to_argb(); FILE *f = fopen(path, "wb"); if (!f) return; fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    for (int i = 0; i < SCR_W * SCR_H; i++) { uint32_t p = plat_argb[i]; fputc(p >> 16, f); fputc(p >> 8, f); fputc(p, f); } fclose(f);
}
/* headless input script: lines "T <ticks> D x y" (mouse down) / "U x y" / "K keycode char" / "S file" (screenshot) / "Q" */
static FILE *script_f; static uint32_t script_next_tick; static char script_line[256]; static int script_has;
static int script_next(PEvt *e, uint32_t ticks) {
    if (!script_f) return 0;
    for (;;) {
        if (!script_has) { if (!fgets(script_line, sizeof script_line, script_f)) { fclose(script_f); script_f = NULL; return 0; } script_has = 1; script_next_tick = 0; unsigned t; if (sscanf(script_line, "T %u", &t) == 1) script_next_tick = t; }
        if (ticks < script_next_tick) return 0;
        script_has = 0; const char *p = strchr(script_line, ' '); if (!p) continue; p++; p = strchr(p, ' '); if (!p) continue; p++;
        int x, y, k, c; char fn[200];
        if (sscanf(p, "D %d %d", &x, &y) == 2) { e->type = PE_DOWN; e->x = x; e->y = y; e->btn = 1; return 1; }
        if (sscanf(p, "U %d %d", &x, &y) == 2) { e->type = PE_UP; e->x = x; e->y = y; e->btn = 0; return 1; }
        if (sscanf(p, "M %d %d", &x, &y) == 2) { e->type = PE_MOVE; e->x = x; e->y = y; return 1; }
        if (sscanf(p, "K %d %d", &k, &c) == 2) { e->type = PE_KEY; e->key = k; e->chr = c; e->mods = 0; return 1; }
        if (sscanf(p, "S %199s", fn) == 1) { write_ppm(fn); fprintf(stderr, "[script] screenshot %s\n", fn); continue; }
        if (p[0] == 'Q') { e->type = PE_QUIT; return 1; }
    }
}
static int plat_poll(PEvt *e, uint32_t ticks) {
    if (script_f || (getenv("CASTLES_SCRIPT") && !script_f && !script_has)) {
        if (!script_f && getenv("CASTLES_SCRIPT")) { static int opened; if (!opened) { opened = 1; script_f = fopen(getenv("CASTLES_SCRIPT"), "r"); } }
        if (script_next(e, ticks)) { if (e->type == PE_DOWN || e->type == PE_UP || e->type == PE_MOVE) { plat_mx = e->x; plat_my = e->y; if (e->type != PE_MOVE) plat_btn = e->btn; } return 1; }
    }
#ifdef HAVE_SDL
    if (headless) return 0;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT: e->type = PE_QUIT; return 1;
        case SDL_MOUSEMOTION: plat_mx = ev.motion.x; plat_my = ev.motion.y; e->type = PE_MOVE; e->x = plat_mx; e->y = plat_my; return 1;
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
            plat_mx = ev.button.x; plat_my = ev.button.y; int down = ev.type == SDL_MOUSEBUTTONDOWN; plat_btn = down; e->type = down ? PE_DOWN : PE_UP; e->x = plat_mx; e->y = plat_my; e->btn = down;
            e->mods = 0; return 1; }
        case SDL_KEYDOWN: case SDL_KEYUP: {
            int mac = sdl2mac(ev.key.keysym.scancode); int down = ev.type == SDL_KEYDOWN; if (mac < 0) break; plat_set_key(mac, down);
            int m = 0; SDL_Keymod km = SDL_GetModState(); if (km & KMOD_GUI) m |= 0x100; if (km & KMOD_CTRL) m |= (km & KMOD_GUI) ? 0 : 0x100; if (km & KMOD_SHIFT) m |= 0x200; if (km & KMOD_CAPS) m |= 0x400; if (km & KMOD_ALT) m |= 0x800;
            plat_mods = m; if (mac == 0x38 || mac == 0x3B || mac == 0x3A || mac == 0x37 || mac == 0x39) break;
            e->type = down ? (ev.key.repeat ? PE_AUTOKEY : PE_KEY) : PE_KEYUP; e->key = mac; e->chr = mac_char(ev.key.keysym); e->mods = m; return 1; }
        default: break;
        }
    }
#endif
    return 0;
}
