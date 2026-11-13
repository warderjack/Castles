#!/usr/bin/env python3
"""Load the CODE segment model of the Castles application and provide analysis helpers."""
import struct, os, sys, json
sys.path.insert(0, os.path.dirname(__file__))
import m68k

ROOT = os.path.join(os.path.dirname(__file__), '..')
RES = os.path.join(ROOT, 'extracted', 'app')

def res(t, i):
    p = os.path.join(RES, t, f'{i}.bin')
    return open(p, 'rb').read() if os.path.exists(p) else None

class Seg:
    pass

def load():
    c0 = res('CODE', 0)
    above, below, jtsize, jtoff = struct.unpack('>IIII', c0[:16])
    entries = []   # jump-table entries: (index, segid, offset)
    for i in range(jtsize // 8):
        o = 16 + i * 8
        off, mv, segid, trap = struct.unpack('>HHHH', c0[o:o + 8])
        assert mv == 0x3F3C and trap == 0xA9F0, (i, hex(mv), hex(trap))
        entries.append((i, segid, off))
    segs = {}
    for sid in range(1, 64):
        b = res('CODE', sid)
        if b is None: continue
        s = Seg()
        s.id = sid; s.data = b
        s.jt_first, s.jt_count = struct.unpack('>HH', b[:4])
        s.crel = None
        cr = res('CREL', sid)
        if cr is not None:
            s.crel = struct.unpack('>%dH' % (len(cr) // 2), cr)
        segs[sid] = s
    import mdrv    # music driver: decrypt + LZSS of the MDRV resource, exactly as the game does at runtime
    md = mdrv.decode() if res('MDRV', 11) is not None else None
    sm = res('SMOD', 0)
    if sm is not None:        # sound module code resource (heap-resident, entered via handle): pseudo-segment 15
        s = Seg(); s.id = 15; s.data = sm; s.jt_first = s.jt_count = 0; s.crel = None; segs[15] = s
    if md is not None:        # music driver: position-independent code resource, entry at offset 0; treated as pseudo-segment 14
        s = Seg(); s.id = 14; s.data = md; s.jt_first = s.jt_count = 0; s.crel = None; segs[14] = s
    return dict(above=above, below=below, jtsize=jtsize, jtoff=jtoff, entries=entries, segs=segs)

def jt_a5off(index):
    """A5-relative offset of the JMP word of jump-table entry `index` (what JSR d(A5) targets)."""
    return 0x20 + index * 8 + 2

if __name__ == '__main__':
    M = load()
    print('above A5 %#x below A5 %#x jt size %#x (%d entries) jt off %#x' % (M['above'], M['below'], M['jtsize'], len(M['entries']), M['jtoff']))
    from collections import Counter
    c = Counter(e[1] for e in M['entries'])
    for sid, s in sorted(M['segs'].items()):
        print('seg %2d: size %6d  hdr jt_first=%#06x jt_count=%d  entries_in_table=%d  crel=%s' % (
            sid, len(s.data), s.jt_first, s.jt_count, c.get(sid, 0), None if s.crel is None else len(s.crel)))
        ids = [i for i, sg, o in M['entries'] if sg == sid]
        if ids:
            print('        entry idx range %d..%d  (first_byte_off=%#x) ' % (min(ids), max(ids), min(ids) * 8))
