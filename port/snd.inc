static int fp_chan_match(uint32_t chan); static int filep_start(uint32_t chan, int fref, uint32_t completion); static void filep_stop(void);
/* ---- Sound Manager (timing-accurate simulated playback; real mixing via SDL audio later) ---- */
static struct { int on; uint32_t chan, hdr; int cur; double t_end; } dbp; static unsigned long dbp_bytes;
static double snd_buf_ms(uint32_t buf, uint32_t hdr) { uint32_t rate = rd32(hdr + 8) >> 16; if (!rate) rate = 22254; return rd32(buf) * 1000.0 / rate; }
static int snd_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    uint32_t sp = SP;
    switch (n) {
    case 0xA800: {
        uint32_t sel = D(0);
        if (sel == 0x0D000008) { uint32_t comp = rd32(sp + 2); int fref = (int16_t)rd16(sp + 20); uint32_t chan = rd32(sp + 22); int e = filep_start(chan, fref, comp); setsp(sp + 26); wr16(sp + 26, (uint16_t)e); return 1; }   /* SndStartFilePlay */
        if (sel == 0x02010008) { if (getenv("SNDLOG")) fprintf(stderr, "  [snd] SndPauseFilePlay\n"); setsp(sp + 4); wr16(sp + 4, 0); return 1; }
        if (sel == 0x03020008) { filep_stop(); setsp(sp + 6); wr16(sp + 6, 0); return 1; }   /* SndStopFilePlay(chan, quietNow) */
        if (sel == 0x03080008) { uint32_t st = rd32(sp); int len = (int16_t)rd16(sp + 4); for (int i = 0; i < len && i < 6; i++) wr8(st + i, i == 3 ? 1 : 0); setsp(sp + 6); wr16(sp + 6, 0); return 1; }   /* SndManagerStatus(len, SMStatus*) */
        if (sel == 0x022C0018) { uint32_t p = rd32(sp); if (p) wr32(p, 0x01000100); setsp(sp + 4); wr16(sp + 4, 0); return 1; }   /* GetDefaultOutputVolume(long *level): full volume both channels (CD release) */
        if (sel == 0x05100008) { uint32_t st = rd32(sp); int len = (int16_t)rd16(sp + 4); for (int i = 0; i < len && i < 64; i++) wr8(st + i, 0); setsp(sp + 10); wr16(sp + 10, 0); return 1; }   /* SndChannelStatus(chan, len, SCStatus*): idle channel (scChannelBusy=0) */
        if (sel == 0x000C0008) { wr32(sp, 0x03008000); return 1; }       /* SndSoundManagerVersion: NumVersion 3.0 */
        if (sel == 0x00200008 || sel == 0x04200008) {        /* SndPlayDoubleBuffer(chan, SndDoubleBufferHeader*): OSErr -- accepted, silent for now */
            uint32_t hdr = rd32(sp); fprintf(stderr, "  SndPlayDoubleBuffer chan=%06x hdr=%06x chans=%d bits=%d rate=%u buf0=%06x buf1=%06x proc=%06x\n", rd32(sp + 4), hdr,
                rd16(hdr), rd16(hdr + 2), rd32(hdr + 8) >> 16, rd32(hdr + 12), rd32(hdr + 16), rd32(hdr + 20));
            { uint32_t b0 = rd32(hdr + 12), b1 = rd32(hdr + 16); fprintf(stderr, "  [snd] buf0: %u bytes flags=%08x   buf1: %u bytes flags=%08x\n", rd32(b0), rd32(b0 + 4), rd32(b1), rd32(b1 + 4)); }
            { uint32_t a4 = rd32(hdr + 20) + 0x3b20; if (getenv("SNDLOG")) fprintf(stderr, "  [snd] driver state a4=%06x [58]=%08x voices=%d+%d\n", a4, rd32(a4 + 0x58), rd16(a4 + 0x11c0), rd16(a4 + 0x11c4)); }
            dbp.on = 1; dbp_bytes = 0; dbp.chan = rd32(sp + 4); dbp.hdr = hdr; dbp.cur = 0; dbp.t_end = -1;
            setsp(sp + 8); wr16(sp + 8, 0); return 1; }
        fprintf(stderr, "  SoundDispatch selector %08x not implemented\n", sel); return 0; }
    case 0xA807: { uint32_t chp = rd32(sp + 10), ch = rd32(chp);      /* SndNewChannel(VAR chan, synth, init, userRoutine): allocate a full zeroed SndChannel (queue[128]) */
        if (getenv("SNDLOG")) fprintf(stderr, "  [snd] SndNewChannel *chan=%06x\n", ch);
        ch = alloc_block(1100); if (ch) memset(ram + ch, 0, 1100);      /* caller-supplied *chan is ignored (the game leaves stale values there) */
        if (ch) { wr32(ch + 8, rd32(sp)); wr16(ch + 34, 128); } wr32(chp, ch); setsp(sp + 14); wr16(sp + 14, ch ? 0 : (uint16_t)-108); return 1; }  /* SndNewChannel */
    case 0xA801: if (fp_chan_match(rd32(sp + 2))) filep_stop(); if (dbp.on && rd32(sp + 2) == dbp.chan) { dbp.on = 0; if (getenv("SNDLOG")) fprintf(stderr, "  [snd] SndDisposeChannel stops double buffer\n"); } setsp(sp + 6); wr16(sp + 6, 0); return 1;                /* SndDisposeChannel */
    case 0xA802: setsp(sp + 14); wr16(sp + 14, 0); return 1;              /* SndAddModifier */
    case 0xA803: { int c = rd16(rd32(sp + 2)); if ((c == 3 || c == 4) && fp_chan_match(rd32(sp + 6))) filep_stop(); } if (getenv("SNDLOG")) fprintf(stderr, "  [snd] SndDoCommand cmd=%04x p1=%04x p2=%08x\n", rd16(rd32(sp)), rd16(rd32(sp) + 2), rd32(rd32(sp) + 4)); setsp(sp + 10); wr16(sp + 10, 0); return 1;              /* SndDoCommand */
    case 0xA804: { int c = rd16(rd32(sp)); if ((c == 3 || c == 4) && fp_chan_match(rd32(sp + 4))) filep_stop(); } if (getenv("SNDLOG")) fprintf(stderr, "  [snd] SndDoImmediate cmd=%04x p1=%04x p2=%08x\n", rd16(rd32(sp)), rd16(rd32(sp) + 2), rd32(rd32(sp) + 4)); setsp(sp + 8); wr16(sp + 8, 0); return 1;                /* SndDoImmediate */
    case 0xA805: if (getenv("SNDLOG")) fprintf(stderr, "  [snd] SndPlay chan=%06x h=%06x async=%d\n", rd32(sp + 6), rd32(sp + 2), rd8(sp)); setsp(sp + 10); wr16(sp + 10, 0); return 1;              /* SndPlay */
    case 0xA806: setsp(sp + 6); wr16(sp + 6, 0); return 1;                /* SndControl */
    default: return 0;
    }
}
