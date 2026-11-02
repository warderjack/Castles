#!/usr/bin/env python3
"""
Motorola 68000/68020 instruction decoder (Motorola syntax output).

decode(buf, off, base) -> Inst
  Inst.addr      absolute address (base+off)
  Inst.size      length in bytes
  Inst.mn        mnemonic (lower-case, no size suffix; condition folded in, e.g. 'bne', 'dbra', 'seq')
  Inst.sz        operand size in bytes (1,2,4) or None
  Inst.ops       list of operands (src first, Motorola order)
  Inst.text()    disassembly text

Operand tuples:
  ('d',n) ('a',n) ('ind',n) ('post',n) ('pre',n) ('disp',n,d)
  ('idx',n,xa,xn,xsz,scale,d8)         brief extension word, base An
  ('full',base,bd,xr,xsz,scale,ind,od) 68020 full extension; base None | ('a',n) | ('pc',)
  ('absw',v) ('absl',v) ('pcd',d,target) ('pcidx',xa,xn,xsz,scale,d8,pcval)
  ('imm',v) ('regs',mask,predec) ('tgt',addr) ('ccr',) ('sr',) ('usp',)
  ('bf', ea, off, width)   off/width are ints or ('d',n)
"""
import struct

class DecodeError(Exception):
    pass

COND = ['t','f','hi','ls','cc','cs','ne','eq','vc','vs','pl','mi','ge','lt','gt','le']

class Inst:
    __slots__ = ('addr','size','mn','sz','ops','words','raw','extra')
    def __init__(self, addr):
        self.addr = addr; self.size = 2; self.mn = '???'; self.sz = None
        self.ops = []; self.words = []; self.raw = b''; self.extra = None
    def text(self):
        return fmt(self)
    def __repr__(self):
        return f'<{self.addr:08x} {fmt(self)}>'

class Rd:
    def __init__(self, buf, off, base):
        self.buf = buf; self.off = off; self.base = base; self.start = off
    def w(self):
        if self.off + 2 > len(self.buf): raise DecodeError('eof')
        v = struct.unpack_from('>H', self.buf, self.off)[0]; self.off += 2; return v
    def sw(self):
        v = self.w(); return v - 0x10000 if v & 0x8000 else v
    def l(self):
        if self.off + 4 > len(self.buf): raise DecodeError('eof')
        v = struct.unpack_from('>I', self.buf, self.off)[0]; self.off += 4; return v
    def sl(self):
        v = self.l(); return v - (1 << 32) if v & 0x80000000 else v
    def pc(self):
        return self.base + self.off   # address of next word to be read

def sx8(v):  return v - 0x100 if v & 0x80 else v
def sx16(v): return v - 0x10000 if v & 0x8000 else v

def ext_ea(rd, mode, reg, sz, pc_hint=None):
    """Decode effective address; may consume extension words."""
    if mode == 0: return ('d', reg)
    if mode == 1: return ('a', reg)
    if mode == 2: return ('ind', reg)
    if mode == 3: return ('post', reg)
    if mode == 4: return ('pre', reg)
    if mode == 5: return ('disp', reg, rd.sw())
    if mode == 6:
        return decode_index(rd, ('a', reg))
    # mode 7
    if reg == 0: return ('absw', rd.sw())
    if reg == 1: return ('absl', rd.l())
    if reg == 2:
        pcv = rd.pc(); d = rd.sw()
        return ('pcd', d, (pcv + d) & 0xFFFFFFFF)
    if reg == 3:
        return decode_index(rd, ('pc',))
    if reg == 4:
        if sz == 1: v = rd.w() & 0xFF
        elif sz == 2: v = rd.w()
        elif sz == 4: v = rd.l()
        elif sz == 8:
            v = (rd.l() << 32) | rd.l()
        elif sz == 12:
            v = (rd.l() << 64) | (rd.l() << 32) | rd.l()
        else: raise DecodeError('imm size')
        return ('imm', v)
    raise DecodeError('bad ea mode 7/%d' % reg)

def decode_index(rd, base):
    pcv = rd.pc()
    ew = rd.w()
    xa = (ew >> 15) & 1; xn = (ew >> 12) & 7; xsz = 4 if (ew >> 11) & 1 else 2
    scale = (ew >> 9) & 3
    if not (ew & 0x100):
        d8 = sx8(ew & 0xFF)
        if base[0] == 'pc':
            return ('pcidx', xa, xn, xsz, scale, d8, pcv)
        return ('idx', base[1], xa, xn, xsz, scale, d8)
    # full format
    bs = (ew >> 7) & 1; is_ = (ew >> 6) & 1; bds = (ew >> 4) & 3; iis = ew & 7
    bd = 0
    if bds == 2: bd = rd.sw()
    elif bds == 3: bd = rd.sl()
    od = 0
    ind = None
    if iis & 3 == 2: od = rd.sw()
    elif iis & 3 == 3: od = rd.sl()
    if iis:
        ind = 'pre' if (not is_ and not (iis & 4)) else ('post' if (not is_ and (iis & 4)) else ('mem' ))
        if is_ and iis: ind = 'mem0'
    xr = None if is_ else (xa, xn)
    b = None if bs else base
    if base[0] == 'pc' and not bs:
        b = ('pc', pcv)
    return ('full', b, bd, xr, xsz, scale, ind, od)

TRAP_NAMES = {}  # filled by traps module on demand

def decode(buf, off, base=0):
    rd = Rd(buf, off, base)
    addr = base + off
    I = Inst(addr)
    op = rd.w()
    I.words = [op]
    hi = op >> 12
    try:
        _decode(rd, I, op, hi)
    except DecodeError:
        raise
    I.size = rd.off - rd.start
    I.raw = bytes(buf[off:off + I.size])
    return I

def sizebits(b):
    return {0: 1, 1: 2, 2: 4}.get(b)

def _decode(rd, I, op, hi):
    mode = (op >> 3) & 7; reg = op & 7; rx = (op >> 9) & 7
    sbits = (op >> 6) & 3
    if hi == 0:
        return _g0(rd, I, op, mode, reg, rx, sbits)
    if hi in (1, 2, 3):
        sz = {1: 1, 3: 2, 2: 4}[hi]
        dmode = (op >> 6) & 7; dreg = rx
        src = ext_ea(rd, mode, reg, sz)
        if dmode == 1:
            if sz == 1: raise DecodeError('movea.b')
            I.mn = 'movea'; I.sz = sz; I.ops = [src, ('a', dreg)]; return
        dst = ext_ea(rd, dmode, dreg, sz)
        I.mn = 'move'; I.sz = sz; I.ops = [src, dst]; return
    if hi == 4:
        return _g4(rd, I, op, mode, reg, rx, sbits)
    if hi == 5:
        if sbits == 3:
            cond = (op >> 8) & 15
            if mode == 1:
                d = rd.pc(); disp = rd.sw()
                I.mn = 'db' + ('ra' if cond == 1 else COND[cond]); I.sz = 2
                I.ops = [('d', reg), ('tgt', (d + disp) & 0xFFFFFFFF)]; return
            if mode == 7 and reg in (2, 3, 4):
                I.mn = 'trap' + COND[cond]
                if reg == 2: I.ops = [('imm', rd.w())]; I.sz = 2
                elif reg == 3: I.ops = [('imm', rd.l())]; I.sz = 4
                return
            I.mn = 's' + COND[cond]; I.sz = 1; I.ops = [ext_ea(rd, mode, reg, 1)]; return
        sz = sizebits(sbits)
        q = rx or 8
        I.mn = 'subq' if (op >> 8) & 1 else 'addq'; I.sz = sz
        I.ops = [('imm', q), ext_ea(rd, mode, reg, sz)]; return
    if hi == 6:
        cond = (op >> 8) & 15; d8 = op & 0xFF
        pc0 = rd.pc()
        if d8 == 0: disp = rd.sw(); I.sz = 2
        elif d8 == 0xFF: disp = rd.sl(); I.sz = 4
        else: disp = sx8(d8); I.sz = 1
        I.mn = 'bra' if cond == 0 else ('bsr' if cond == 1 else 'b' + COND[cond])
        I.ops = [('tgt', (pc0 + disp) & 0xFFFFFFFF)]; return
    if hi == 7:
        if op & 0x100: raise DecodeError('moveq')
        I.mn = 'moveq'; I.sz = 4; I.ops = [('imm', sx8(op & 0xFF) & 0xFFFFFFFF), ('d', rx)]; return
    if hi in (8, 0xC):
        return _g8c(rd, I, op, mode, reg, rx, sbits, hi)
    if hi in (9, 0xD):
        name = 'sub' if hi == 9 else 'add'
        opm = (op >> 6) & 7
        if sbits == 3:
            sz = 2 if opm == 3 else 4
            I.mn = name + 'a'; I.sz = sz; I.ops = [ext_ea(rd, mode, reg, sz), ('a', rx)]; return
        sz = sizebits(sbits)
        if op & 0x100:
            if mode in (0, 1):
                I.mn = name + 'x'; I.sz = sz
                I.ops = [('d', reg), ('d', rx)] if mode == 0 else [('pre', reg), ('pre', rx)]; return
            I.mn = name; I.sz = sz; I.ops = [('d', rx), ext_ea(rd, mode, reg, sz)]; return
        I.mn = name; I.sz = sz; I.ops = [ext_ea(rd, mode, reg, sz), ('d', rx)]; return
    if hi == 0xA:
        I.mn = 'aline'; I.sz = None; I.ops = [('imm', op)]; return
    if hi == 0xB:
        if sbits == 3:
            sz = 2 if not (op & 0x100) else 4
            I.mn = 'cmpa'; I.sz = sz; I.ops = [ext_ea(rd, mode, reg, sz), ('a', rx)]; return
        sz = sizebits(sbits)
        if op & 0x100:
            if mode == 1:
                I.mn = 'cmpm'; I.sz = sz; I.ops = [('post', reg), ('post', rx)]; return
            I.mn = 'eor'; I.sz = sz; I.ops = [('d', rx), ext_ea(rd, mode, reg, sz)]; return
        I.mn = 'cmp'; I.sz = sz; I.ops = [ext_ea(rd, mode, reg, sz), ('d', rx)]; return
    if hi == 0xE:
        return _ge(rd, I, op, mode, reg, rx, sbits)
    if hi == 0xF:
        I.mn = 'fline'; I.ops = [('imm', op)]
        # coprocessor ext words not decoded; mark
        return
    raise DecodeError('?')

def _g0(rd, I, op, mode, reg, rx, sbits):
    if op & 0x100:
        if mode == 1:
            opm = (op >> 6) & 7
            disp = rd.sw()
            I.mn = 'movep'; I.sz = 2 if opm in (4, 6) else 4
            if opm in (4, 5): I.ops = [('disp', reg, disp), ('d', rx)]
            else: I.ops = [('d', rx), ('disp', reg, disp)]
            return
        names = ['btst', 'bchg', 'bclr', 'bset']
        I.mn = names[sbits]; I.sz = 4 if mode == 0 else 1
        I.ops = [('d', rx), ext_ea(rd, mode, reg, I.sz)]; return
    top = (op >> 9) & 7
    if top == 4:
        names = ['btst', 'bchg', 'bclr', 'bset']
        bit = rd.w() & 0xFF
        I.mn = names[sbits]; I.sz = 4 if mode == 0 else 1
        I.ops = [('imm', bit), ext_ea(rd, mode, reg, I.sz)]; return
    if op in (0x003C, 0x007C, 0x023C, 0x027C, 0x0A3C, 0x0A7C):
        nm = {0: 'ori', 2: 'andi', 0xA: 'eori'}[(op >> 8) & 0xF]
        if op & 0x40:
            I.mn = nm; I.sz = 2; I.ops = [('imm', rd.w()), ('sr',)]
        else:
            I.mn = nm; I.sz = 1; I.ops = [('imm', rd.w() & 0xFF), ('ccr',)]
        return
    if sbits == 3:
        # 68020: cmp2/chk2, cas, moves, rtm, callm
        sz = {0: 1, 1: 2, 2: 4}.get((op >> 9) & 3)
        if top in (0, 1, 2, 3) and sz and (op >> 11) & 1 == 0:
            ew = rd.w()
            I.mn = 'chk2' if ew & 0x800 else 'cmp2'; I.sz = sz
            I.ops = [ext_ea(rd, mode, reg, sz), ('a' if ew & 0x8000 else 'd', (ew >> 12) & 7)]; return
        if (op >> 11) & 1 and top in (5, 6, 7):
            sz = {1: 1, 2: 2, 3: 4}[(op >> 9) & 3]
            ew = rd.w()
            I.mn = 'cas'; I.sz = sz
            I.ops = [('d', ew & 7), ('d', (ew >> 6) & 7), ext_ea(rd, mode, reg, sz)]; return
        raise DecodeError('g0 sbits3')
    sz = sizebits(sbits)
    names = {0: 'ori', 1: 'andi', 2: 'subi', 3: 'addi', 5: 'eori', 6: 'cmpi'}
    if top in names:
        if sz == 1: imm = rd.w() & 0xFF
        elif sz == 2: imm = rd.w()
        else: imm = rd.l()
        I.mn = names[top]; I.sz = sz; I.ops = [('imm', imm), ext_ea(rd, mode, reg, sz)]; return
    if top == 7:
        ew = rd.w()
        I.mn = 'moves'; I.sz = sz
        r = ('a' if ew & 0x8000 else 'd', (ew >> 12) & 7)
        e = ext_ea(rd, mode, reg, sz)
        I.ops = [r, e] if ew & 0x800 else [e, r]; return
    raise DecodeError('g0')

def _g4(rd, I, op, mode, reg, rx, sbits):
    # exact opcodes first
    if op == 0x4E71: I.mn = 'nop'; return
    if op == 0x4E75: I.mn = 'rts'; return
    if op == 0x4E73: I.mn = 'rte'; return
    if op == 0x4E77: I.mn = 'rtr'; return
    if op == 0x4E76: I.mn = 'trapv'; return
    if op == 0x4E70: I.mn = 'reset'; return
    if op == 0x4AFC: I.mn = 'illegal'; return
    if op == 0x4E72: I.mn = 'stop'; I.ops = [('imm', rd.w())]; return
    if op == 0x4E74: I.mn = 'rtd'; I.ops = [('imm', rd.sw())]; return
    if (op & 0xFFF0) == 0x4E40: I.mn = 'trap'; I.ops = [('imm', op & 15)]; return
    if (op & 0xFFF8) == 0x4E50: I.mn = 'link'; I.sz = 2; I.ops = [('a', reg), ('imm', rd.sw())]; return
    if (op & 0xFFF8) == 0x4808: I.mn = 'link'; I.sz = 4; I.ops = [('a', reg), ('imm', rd.sl())]; return
    if (op & 0xFFF8) == 0x4E58: I.mn = 'unlk'; I.ops = [('a', reg)]; return
    if (op & 0xFFF8) == 0x4E60: I.mn = 'move'; I.sz = 4; I.ops = [('a', reg), ('usp',)]; return
    if (op & 0xFFF8) == 0x4E68: I.mn = 'move'; I.sz = 4; I.ops = [('usp',), ('a', reg)]; return
    if (op & 0xFFFE) == 0x4E7A:
        ew = rd.w(); I.mn = 'movec'; I.sz = 4
        r = ('a' if ew & 0x8000 else 'd', (ew >> 12) & 7); cr = ('imm', ew & 0xFFF)
        I.ops = [cr, r] if not (op & 1) else [r, cr]; return
    if (op & 0xFFF8) == 0x4848: I.mn = 'bkpt'; I.ops = [('imm', reg)]; return
    if (op & 0xFFF8) == 0x4880: I.mn = 'ext'; I.sz = 2; I.ops = [('d', reg)]; return
    if (op & 0xFFF8) == 0x48C0: I.mn = 'ext'; I.sz = 4; I.ops = [('d', reg)]; return
    if (op & 0xFFF8) == 0x49C0: I.mn = 'extb'; I.sz = 4; I.ops = [('d', reg)]; return
    if (op & 0xFFF8) == 0x4840: I.mn = 'swap'; I.sz = 2; I.ops = [('d', reg)]; return
    if (op & 0xFFC0) == 0x4E80: I.mn = 'jsr'; I.ops = [ext_ea(rd, mode, reg, 4)]; return
    if (op & 0xFFC0) == 0x4EC0: I.mn = 'jmp'; I.ops = [ext_ea(rd, mode, reg, 4)]; return
    if (op & 0xFFC0) == 0x4840: I.mn = 'pea'; I.sz = 4; I.ops = [ext_ea(rd, mode, reg, 4)]; return
    if (op & 0xFB80) == 0x4880:  # MOVEM
        sz = 4 if op & 0x40 else 2
        mask = rd.w()
        ea = ext_ea(rd, mode, reg, sz)
        I.mn = 'movem'; I.sz = sz
        if op & 0x400: I.ops = [ea, ('regs', mask, False)]
        else: I.ops = [('regs', mask, mode == 4), ea]
        return
    if (op & 0xF1C0) == 0x41C0:
        I.mn = 'lea'; I.sz = 4; I.ops = [ext_ea(rd, mode, reg, 4), ('a', rx)]; return
    if (op & 0xF1C0) == 0x4180:
        I.mn = 'chk'; I.sz = 2; I.ops = [ext_ea(rd, mode, reg, 2), ('d', rx)]; return
    if (op & 0xF1C0) == 0x4100:
        I.mn = 'chk'; I.sz = 4; I.ops = [ext_ea(rd, mode, reg, 4), ('d', rx)]; return
    if (op & 0xFFC0) in (0x4C00, 0x4C40):
        ew = rd.w()
        ea = ext_ea(rd, mode, reg, 4)
        sign = (ew >> 11) & 1; is64 = (ew >> 10) & 1
        dl = (ew >> 12) & 7; dh = ew & 7
        if (op & 0xFFC0) == 0x4C00:
            I.mn = 'muls' if sign else 'mulu'
        else:
            I.mn = 'divs' if sign else 'divu'
        I.sz = 4
        I.ops = [ea, ('d', dl)] if not is64 else [ea, ('dpair', dh, dl)]
        I.extra = (ew, is64)
        return
    s = (op >> 8) & 0xF
    if sbits == 3:
        if s == 0: I.mn = 'move'; I.sz = 2; I.ops = [('sr',), ext_ea(rd, mode, reg, 2)]; return
        if s == 2: I.mn = 'move'; I.sz = 2; I.ops = [('ccr',), ext_ea(rd, mode, reg, 2)]; return
        if s == 4: I.mn = 'move'; I.sz = 2; I.ops = [ext_ea(rd, mode, reg, 2), ('ccr',)]; return
        if s == 6: I.mn = 'move'; I.sz = 2; I.ops = [ext_ea(rd, mode, reg, 2), ('sr',)]; return
        if s == 0xA: I.mn = 'tas'; I.sz = 1; I.ops = [ext_ea(rd, mode, reg, 1)]; return
        raise DecodeError('g4 s3')
    sz = sizebits(sbits)
    names = {0: 'negx', 2: 'clr', 4: 'neg', 6: 'not', 0xA: 'tst'}
    if s in names:
        I.mn = names[s]; I.sz = sz; I.ops = [ext_ea(rd, mode, reg, sz)]; return
    if op & 0xFFC0 == 0x4800:
        I.mn = 'nbcd'; I.sz = 1; I.ops = [ext_ea(rd, mode, reg, 1)]; return
    raise DecodeError('g4')

def _g8c(rd, I, op, mode, reg, rx, sbits, hi):
    name = 'or' if hi == 8 else 'and'
    opm = (op >> 6) & 7
    if opm == 3:
        I.mn = 'divu' if hi == 8 else 'mulu'; I.sz = 2; I.ops = [ext_ea(rd, mode, reg, 2), ('d', rx)]; return
    if opm == 7:
        I.mn = 'divs' if hi == 8 else 'muls'; I.sz = 2; I.ops = [ext_ea(rd, mode, reg, 2), ('d', rx)]; return
    if hi == 8:
        if opm == 4 and mode in (0, 1):
            I.mn = 'sbcd'; I.sz = 1
            I.ops = [('d', reg), ('d', rx)] if mode == 0 else [('pre', reg), ('pre', rx)]; return
        if opm in (5, 6) and mode in (0, 1):
            I.mn = 'pack' if opm == 5 else 'unpk'; I.sz = None
            adj = rd.w()
            I.ops = ([('d', reg), ('d', rx)] if mode == 0 else [('pre', reg), ('pre', rx)]) + [('imm', adj)]; return
    else:
        if opm == 4 and mode in (0, 1):
            I.mn = 'abcd'; I.sz = 1
            I.ops = [('d', reg), ('d', rx)] if mode == 0 else [('pre', reg), ('pre', rx)]; return
        if opm == 5 and mode in (0, 1):
            I.mn = 'exg'; I.sz = 4
            I.ops = [('d', rx), ('d', reg)] if mode == 0 else [('a', rx), ('a', reg)]; return
        if opm == 6 and mode == 1:
            I.mn = 'exg'; I.sz = 4; I.ops = [('d', rx), ('a', reg)]; return
    sz = sizebits(sbits)
    if opm >= 4:
        I.mn = name; I.sz = sz; I.ops = [('d', rx), ext_ea(rd, mode, reg, sz)]; return
    I.mn = name; I.sz = sz; I.ops = [ext_ea(rd, mode, reg, sz), ('d', rx)]; return

def _ge(rd, I, op, mode, reg, rx, sbits):
    if sbits == 3:
        if op & 0x0800:   # bit-field ops (68020)
            sel = (op >> 8) & 7
            names = {0: 'bftst', 1: 'bfextu', 2: 'bfchg', 3: 'bfexts', 4: 'bfclr', 5: 'bfffo', 6: 'bfset', 7: 'bfins'}
            ew = rd.w()
            ea = ext_ea(rd, mode, reg, 1)
            off = ('d', (ew >> 6) & 7) if ew & 0x800 else (ew >> 6) & 31
            wd = ('d', ew & 7) if ew & 0x20 else (ew & 31) or 32
            I.mn = names[sel]; I.sz = None
            dn = ('d', (ew >> 12) & 7)
            bf = ('bf', ea, off, wd)
            if sel in (1, 3, 5): I.ops = [bf, dn]
            elif sel == 7: I.ops = [dn, bf]
            else: I.ops = [bf]
            return
        typ = (op >> 9) & 3
        names = ['as', 'ls', 'rox', 'ro']
        I.mn = names[typ] + ('l' if op & 0x100 else 'r'); I.sz = 2
        I.ops = [ext_ea(rd, mode, reg, 2)]; return
    sz = sizebits(sbits)
    typ = (op >> 3) & 3
    names = ['as', 'ls', 'rox', 'ro']
    I.mn = names[typ] + ('l' if op & 0x100 else 'r'); I.sz = sz
    if op & 0x20: I.ops = [('d', rx), ('d', reg)]
    else: I.ops = [('imm', rx or 8), ('d', reg)]

# ---------------------------------------------------------------- formatting
def hx(v, width=None):
    if v < 0: return '-$%x' % (-v)
    return '$%x' % v

def fop(o):
    k = o[0]
    if k == 'd': return 'd%d' % o[1]
    if k == 'a': return 'a%d' % o[1] if o[1] != 7 else 'sp'
    if k == 'ind': return '(%s)' % fop(('a', o[1]))
    if k == 'post': return '(%s)+' % fop(('a', o[1]))
    if k == 'pre': return '-(%s)' % fop(('a', o[1]))
    if k == 'disp': return '%s(%s)' % (hx(o[2]), fop(('a', o[1])))
    if k == 'idx':
        x = ('a' if o[2] else 'd') + str(o[3]) + ('.w' if o[4] == 2 else '.l') + ('*%d' % (1 << o[5]) if o[5] else '')
        return '%s(%s,%s)' % (hx(o[6]), fop(('a', o[1])), x)
    if k == 'full':
        _, b, bd, xr, xsz, sc, ind, od = o
        parts = []
        if b is None: parts.append('zb')
        elif b[0] == 'pc': parts.append('pc')
        else: parts.append(fop(b))
        if xr: parts.append(('a' if xr[0] else 'd') + str(xr[1]) + ('.w' if xsz == 2 else '.l') + ('*%d' % (1 << sc) if sc else ''))
        return '([%s,%s],%s)' % (hx(bd), ','.join(parts), hx(od)) if ind else '(%s,%s)' % (hx(bd), ','.join(parts))
    if k == 'absw': return '%s.w' % hx(o[1] & 0xFFFFFFFF if o[1] >= 0 else o[1])
    if k == 'absl': return '%s.l' % hx(o[1])
    if k == 'pcd': return '%s(pc)' % hx(o[1])
    if k == 'pcidx':
        x = ('a' if o[1] else 'd') + str(o[2]) + ('.w' if o[3] == 2 else '.l') + ('*%d' % (1 << o[4]) if o[4] else '')
        return '%s(pc,%s)' % (hx(o[5]), x)
    if k == 'imm': return '#%s' % hx(o[1])
    if k == 'regs':
        mask = o[1]
        if o[2]:  # predecrement: bit0 = a7 ... bit15 = d0
            names = ['a7','a6','a5','a4','a3','a2','a1','a0','d7','d6','d5','d4','d3','d2','d1','d0']
            lst = [names[i] for i in range(16) if mask >> i & 1]
            # reorder to d0..a7 for display
            lst = lst[::-1]
        else:
            names = ['d0','d1','d2','d3','d4','d5','d6','d7','a0','a1','a2','a3','a4','a5','a6','a7']
            lst = [names[i] for i in range(16) if mask >> i & 1]
        return '/'.join(lst)
    if k == 'tgt': return '$%x' % o[1]
    if k == 'ccr': return 'ccr'
    if k == 'sr': return 'sr'
    if k == 'usp': return 'usp'
    if k == 'dpair': return 'd%d:d%d' % (o[1], o[2])
    if k == 'bf':
        off = 'd%d' % o[2][1] if isinstance(o[2], tuple) else str(o[2])
        wd = 'd%d' % o[3][1] if isinstance(o[3], tuple) else str(o[3])
        return '%s{%s:%s}' % (fop(o[1]), off, wd)
    return str(o)

SZ = {1: '.b', 2: '.w', 4: '.l', None: ''}

def fmt(I):
    mn = I.mn
    if mn == 'aline':
        return 'dc.w    $%04x   ; A-line' % I.ops[0][1]
    if mn in ('bra', 'bsr') or (mn[0] == 'b' and mn[1:] in COND and mn != 'bsr'):
        s = {1: '.s', 2: '.w', 4: '.l'}.get(I.sz, '')
        return '%-7s %s' % (mn + s, fop(I.ops[0]))
    if mn.startswith('db'):
        return '%-7s %s' % (mn, ','.join(fop(o) for o in I.ops))
    s = SZ.get(I.sz, '') if mn not in ('swap',) else ''
    if mn in ('movem',) and I.sz == 2: s = '.w'
    ops = ','.join(fop(o) for o in I.ops)
    return ('%-7s %s' % (mn + s, ops)).rstrip()

if __name__ == '__main__':
    import sys
    data = open(sys.argv[1], 'rb').read()
    off = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0
    n = int(sys.argv[3], 0) if len(sys.argv) > 3 else 40
    while n > 0 and off < len(data):
        try:
            i = decode(data, off, 0)
        except DecodeError as e:
            print('%08x  .word $%04x   ; %s' % (off, struct.unpack_from('>H', data, off)[0], e)); off += 2; n -= 1; continue
        print('%08x  %-24s %s' % (i.addr, ' '.join('%04x' % w for w in struct.unpack('>%dH' % (i.size // 2), i.raw)), i.text()))
        off += i.size; n -= 1
