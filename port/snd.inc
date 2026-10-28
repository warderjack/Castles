/* ---- Sound Manager (timing-accurate simulated playback; real mixing via SDL audio later) ---- */
static struct { int on; uint32_t chan, hdr; int cur; double t_end; } dbp;
static double snd_buf_ms(uint32_t buf, uint32_t hdr) { uint32_t rate = rd32(hdr + 8) >> 16; if (!rate) rate = 22254; return rd32(buf) * 1000.0 / rate; }
static int snd_dispatch(uint16_t w, uint16_t n, uint32_t ret) {
    uint32_t sp = SP;
    switch (n) {
    case 0xA800: {
        uint32_t sel = D(0);
        if (sel == 0x000C0008) { wr32(sp, 0x03008000); return 1; }       /* SndSoundManagerVersion: NumVersion 3.0 */
        if (sel == 0x00200008) {        /* SndPlayDoubleBuffer(chan, SndDoubleBufferHeader*): OSErr -- accepted, silent for now */
            uint32_t hdr = rd32(sp); fprintf(stderr, "  SndPlayDoubleBuffer chan=%06x hdr=%06x chans=%d bits=%d rate=%u buf0=%06x buf1=%06x proc=%06x\n", rd32(sp + 4), hdr,
                rd16(hdr), rd16(hdr + 2), rd32(hdr + 8) >> 16, rd32(hdr + 12), rd32(hdr + 16), rd32(hdr + 20));
            dbp.on = 1; dbp.chan = rd32(sp + 4); dbp.hdr = hdr; dbp.cur = 0; dbp.t_end = -1;
            setsp(sp + 8); wr16(sp + 8, 0); return 1; }
        fprintf(stderr, "  SoundDispatch selector %08x not implemented\n", sel); return 0; }
    case 0xA807: { uint32_t chp = rd32(sp + 10); (void)chp; uint32_t ch = alloc_block(32 + 64); wr32(chp, ch); setsp(sp + 14); wr16(sp + 14, 0); return 1; }  /* SndNewChannel */
    case 0xA801: setsp(sp + 6); wr16(sp + 6, 0); return 1;                /* SndDisposeChannel */
    case 0xA802: setsp(sp + 14); wr16(sp + 14, 0); return 1;              /* SndAddModifier */
    case 0xA803: setsp(sp + 10); wr16(sp + 10, 0); return 1;              /* SndDoCommand */
    case 0xA804: setsp(sp + 8); wr16(sp + 8, 0); return 1;                /* SndDoImmediate */
    case 0xA805: setsp(sp + 10); wr16(sp + 10, 0); return 1;              /* SndPlay */
    case 0xA806: setsp(sp + 6); wr16(sp + 6, 0); return 1;                /* SndControl */
    default: return 0;
    }
}
