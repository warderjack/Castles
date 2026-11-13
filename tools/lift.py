#!/usr/bin/env python3
"""
68k -> C translator for the Castles CODE segments.

Generates, per segment, a C file with one function per routine found by tools/analyze.py.
All machine state lives in the globals declared in port/liftrt.h (D[], A[], condition flags) and in the
guest RAM image, with the *real* 68k stack discipline (return addresses are pushed/popped), so that
translated code and the Musashi interpreter can hand control to each other at any routine boundary.

Anything the translator is not sure about becomes `LBAIL(pc)`: control returns to the interpreter at
that instruction.  Output:  port/gen/lift_seg<N>.c   port/gen/lift_tab.c
"""
import sys, os, json, struct, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze, m68k

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'port', 'gen')

MASK = {1: '0xFFu', 2: '0xFFFFu', 4: '0xFFFFFFFFu'}
CONDS = {'hi': 'LC_HI', 'ls': 'LC_LS', 'cc': 'LC_CC', 'cs': 'LC_CS', 'ne': 'LC_NE', 'eq': 'LC_EQ', 'vc': 'LC_VC', 'vs': 'LC_VS',
         'pl': 'LC_PL', 'mi': 'LC_MI', 'ge': 'LC_GE', 'lt': 'LC_LT', 'gt': 'LC_GT', 'le': 'LC_LE', 't': '1', 'f': '0'}


class NoLift(Exception):
    pass


class Gen:
    def __init__(self, P, relocs):
        self.P = P
        self.relocs = {int(k): {o: kind for o, kind in v} for k, v in relocs.items()}
        self.segs = sorted(P.mem)

    # ---- address helpers ----
    def seg_off(self, addr):
        sid = analyze.seg_of(self.P, addr)
        if sid is None: return None
        return sid, addr - analyze.SEGBASE(sid)

    def ra(self, addr):
        so = self.seg_off(addr)
        if so is None: raise NoLift('addr outside segments %x' % addr)
        return '(SB[%d]+0x%x)' % so

    def reloc_at(self, inst):
        """relocation kind for a long inside this instruction, or None"""
        so = self.seg_off(inst.addr)
        r = self.relocs.get(so[0], {})
        for o in range(so[1], so[1] + inst.size, 2):
            if o in r: return r[o], o - so[1]
        return None

    # ---- operand codegen ----
    def ea_expr(self, op, sz, inst, ctx, ev='ea'):
        """Return (prelude statements, address C expression) for a memory operand. ctx carries 'reloc'."""
        k = op[0]
        if k == 'ind': return [], 'A[%d]' % op[1]
        if k == 'post':
            inc = 2 if (sz == 1 and op[1] == 7) else sz
            return ['%s = A[%d]; A[%d] += %d;' % (ev, op[1], op[1], inc)], ev
        if k == 'pre':
            dec = 2 if (sz == 1 and op[1] == 7) else sz
            return ['A[%d] -= %d; %s = A[%d];' % (op[1], dec, ev, op[1])], ev
        if k == 'disp': return [], '(A[%d]+(u32)%d)' % (op[1], op[2])
        if k == 'idx':
            _, n, xa, xn, xsz, scale, d8 = op
            x = ('A[%d]' if xa else 'D[%d]') % xn
            if xsz == 2: x = '(u32)(s32)(s16)%s' % x
            return [], '(A[%d]+(u32)%d+(%s<<%d))' % (n, d8, x, scale)
        if k == 'absw': return [], '(u32)%d' % op[1]
        if k == 'absl': return [], self.reloc_wrap(op[1], ctx)
        if k == 'pcd': return [], self.ra(op[2])
        if k == 'pcidx':
            _, xa, xn, xsz, scale, d8, pcv = op
            x = ('A[%d]' if xa else 'D[%d]') % xn
            if xsz == 2: x = '(u32)(s32)(s16)%s' % x
            return [], '(%s+(%s<<%d))' % (self.ra(pcv + d8), x, scale)
        if k == 'full':
            _, b, bd, xr, xsz, scale, ind, od = op
            if b is None: base = '0'
            elif b[0] == 'pc': base = self.ra(b[1])
            else: base = 'A[%d]' % b[1]
            idx = '0'
            if xr:
                x = ('A[%d]' if xr[0] else 'D[%d]') % xr[1]
                if xsz == 2: x = '(u32)(s32)(s16)%s' % x
                idx = '(%s<<%d)' % (x, scale)
            if ind is None: return [], '(%s+(u32)%d+%s)' % (base, bd, idx)
            if ind == 'pre': return ['%s = m32(%s+(u32)%d+%s)+(u32)%d;' % (ev, base, bd, idx, od)], ev
            if ind == 'post': return ['%s = m32(%s+(u32)%d)+%s+(u32)%d;' % (ev, base, bd, idx, od)], ev
            if ind == 'mem0': return ['%s = m32(%s+(u32)%d)+(u32)%d;' % (ev, base, bd, od)], ev
            raise NoLift('full ind ' + str(ind))
        raise NoLift('ea ' + k)

    def reloc_wrap(self, v, ctx):
        r = ctx.get('reloc')
        if r and not ctx.get('reloc_used') and (v & 0xFFFFFFFF) == ctx['reloc_val']:
            ctx['reloc_used'] = True
            if r[0] == 'S1': return '(SB[1]+0x%xu)' % (v & 0xFFFFFFFF)
            return '(REL_%s+0x%xu)' % (r[0], v & 0xFFFFFFFF)
        return '0x%xu' % (v & 0xFFFFFFFF)

    def read(self, op, sz, inst, ctx, signext=False):
        """C expression (u32) for the value of operand; prelude statements appended to ctx['pre']"""
        k = op[0]
        if k == 'd':
            e = 'D[%d]' % op[1]
            return '(%s&%s)' % (e, MASK[sz]) if sz != 4 else e
        if k == 'a':
            e = 'A[%d]' % op[1]
            return '(%s&%s)' % (e, MASK[sz]) if sz != 4 else e
        if k == 'imm':
            v = op[1] & 0xFFFFFFFF if sz == 4 else op[1] & ((1 << (8 * sz)) - 1)
            if sz == 4: return self.reloc_wrap(v, ctx)
            return '0x%xu' % v
        pre, a = self.ea_expr(op, sz, inst, ctx)
        ctx['pre'] += pre
        ctx['n'] = ctx.get('n', 0) + 1
        name = 'sv%d_' % ctx['n']
        ctx['pre'].append('u32 %s = m%d(%s);' % (name, 8 * sz, a))
        return name

    def write(self, op, sz, val, inst, ctx, ea_done=None):
        """statement writing val (u32 expr) to operand"""
        k = op[0]
        if k == 'd':
            n = op[1]
            if sz == 4: return 'D[%d] = %s;' % (n, val)
            m = MASK[sz]
            return 'D[%d] = (D[%d]&~%s)|((%s)&%s);' % (n, n, m, val, m)
        if k == 'a':
            return 'A[%d] = %s;' % (op[1], val)
        pre, a = ea_done if ea_done else self.ea_expr(op, sz, inst, ctx, 'ed')
        if not ea_done: ctx['pre'] += pre
        return 'w%d(%s, %s);' % (8 * sz, a, val)

    def sxv(self, e, sz):
        if sz == 1: return '((u32)(s32)(s8)(%s))' % e
        if sz == 2: return '((u32)(s32)(s16)(%s))' % e
        return '(%s)' % e

    # ---- control flow target ----
    def jump_to(self, tgt, fset, labels, cur=None):
        if tgt in fset:
            labels.add(tgt)
            if cur is not None and tgt <= cur: return 'LLOOP(%s); goto L%x;' % (self.ra(tgt), tgt)
            return 'goto L%x;' % tgt
        return 'LTAIL(%s);' % self.ra(tgt)

    # ---- one instruction ----
    def inst(self, I, fn, fset, labels, entries):
        mn, sz, ops = I.mn, I.sz, I.ops
        ctx = {'pre': [], 'reloc': self.reloc_at(I)}
        if ctx['reloc']:
            so = self.seg_off(I.addr)
            ctx['reloc_val'] = struct.unpack_from('>I', self.P.mem[so[0]], so[1] + ctx['reloc'][1])[0]
        nxt = I.addr + I.size
        class _R:
            pass
        RET = None
        out = []
        S = out.append
        RET = self.ra(nxt) if analyze.seg_of(self.P, nxt) is not None else '0u'

        def done(extra=None):
            body = ctx['pre'] + out
            return body

        if mn == 'aline':
            w = ops[0][1]
            S('LTRAP(0x%04x, %s, %s);' % (w, self.ra(I.addr), RET))
            return done()
        if mn == 'nop':
            return []
        if mn in ('bra', 'bsr') or (mn[0] == 'b' and mn[1:] in CONDS and mn not in ('bset', 'bclr', 'btst', 'bchg', 'bfextu', 'bfexts', 'bfins', 'bfffo', 'bfclr', 'bfset', 'bfchg', 'bftst')):
            tgt = ops[0][1]
            if mn == 'bsr':
                S('LSCALL(%s, %s);' % (RET, self.fname(tgt)) if tgt in entries else 'LCALL(%s, %s);' % (self.ra(tgt), RET))
            elif mn == 'bra':
                S(self.jump_to(tgt, fset, labels, I.addr))
            else:
                S('if (%s) { %s }' % (CONDS[mn[1:]], self.jump_to(tgt, fset, labels, I.addr)))
            return done()
        if mn.startswith('db'):
            cc = mn[2:]
            dn, tgt = ops[0][1], ops[1][1]
            cond = '0' if cc == 'ra' else CONDS['f' if cc == 'f' else cc]
            S('if (!(%s)) { u32 w_ = ((D[%d]&0xFFFFu)-1u)&0xFFFFu; D[%d] = (D[%d]&0xFFFF0000u)|w_; if (w_ != 0xFFFFu) { %s } }' % (cond, dn, dn, dn, self.jump_to(tgt, fset, labels, I.addr)))
            return done()
        if mn == 'rts':
            S('LRTS();')
            return done()
        if mn == 'rtd':
            S('LRTD(%d);' % ops[0][1])
            return done()
        if mn == 'jsr':
            o = ops[0]
            if o[0] == 'pcd':
                tgt = o[2]
                if tgt in entries: S('LSCALL(%s, %s);' % (RET, self.fname(tgt)))
                else: S('LCALL(%s, %s);' % (self.ra(tgt), RET))
            else:
                pre, a = self.ea_expr(o, 4, I, ctx)
                ctx['pre'] += pre
                S('LCALL(%s, %s);' % (a, RET))
            return done()
        if mn == 'jmp':
            o = ops[0]
            if o[0] == 'pcd':
                tgt = o[2]
                if tgt in fset: S(self.jump_to(tgt, fset, labels, I.addr))
                else: S('LTAIL(%s);' % self.ra(tgt))
            else:
                pre, a = self.ea_expr(o, 4, I, ctx)
                ctx['pre'] += pre
                tg = getattr(self.P, 'jmp_tables', {}).get(I.addr)
                if tg and all(t in fset for t in tg):
                    S('{ u32 t_ = %s; switch (t_ - SB[LSEG]) {' % a)
                    done_t = set()
                    for t in tg:
                        if t in done_t: continue
                        done_t.add(t); labels.add(t)
                        S('case 0x%x: goto L%x;' % (self.seg_off(t)[1], t))
                    S('default: LJMP_DYN(t_); } }')
                else:
                    S('LJMP_DYN(%s);' % a)
            return done()
        if mn == 'link':
            n = ops[0][1]
            S('A[7] -= 4; w32(A[7], A[%d]); A[%d] = A[7]; A[7] += (u32)%d;' % (n, n, ops[1][1]))
            return done()
        if mn == 'unlk':
            n = ops[0][1]
            S('A[7] = A[%d]; A[%d] = m32(A[7]); A[7] += 4;' % (n, n))
            return done()
        if mn == 'lea':
            pre, a = self.ea_expr(ops[0], 4, I, ctx)
            ctx['pre'] += pre
            S('A[%d] = %s;' % (ops[1][1], a))
            return done()
        if mn == 'pea':
            pre, a = self.ea_expr(ops[0], 4, I, ctx)
            ctx['pre'] += pre
            S('{ u32 v_ = %s; A[7] -= 4; w32(A[7], v_); }' % a)
            return done()
        if mn == 'moveq':
            S('D[%d] = (u32)(s32)%d; LNZ(D[%d], 4);' % (ops[1][1], m68k.sx8(ops[0][1] & 0xFF), ops[1][1]))
            S('fV = fC = 0;')
            return done()
        if mn == 'move' and len(ops) == 2 and ops[1][0] == 'sr':
            S('lift_set_sr(%s);' % self.read(ops[0], 2, I, ctx))
            return done()
        if mn == 'move' and len(ops) == 2 and ops[0][0] == 'sr':
            S(self.write(ops[1], 2, 'lift_get_sr()', I, ctx))
            return done()
        if mn in ('move', 'movea'):
            src = self.read(ops[0], sz, I, ctx)
            dst = ops[1]
            if mn == 'movea':
                if sz == 2: src = self.sxv(src, 2)
                S('A[%d] = %s;' % (dst[1], src))
                return done()
            # source evaluated first (and its side effects), then destination ea
            S('{ u32 v_ = %s;' % src)
            S(self.write(dst, sz, 'v_', I, ctx))
            S('LNZ(v_, %d); fV = fC = 0; }' % sz)
            # note: flags must be set even if write has pre statements; order is irrelevant
            return self.move_fix(ctx, out)
        if mn == 'clr':
            S(self.write(ops[0], sz, '0', I, ctx))
            S('fN = 0; fZ = 1; fV = fC = 0;')
            return done()
        if mn == 'tst':
            v = self.read(ops[0], sz, I, ctx)
            S('LNZ(%s, %d); fV = fC = 0;' % (v, sz))
            return done()
        if mn in ('andi', 'ori', 'eori') and len(ops) == 2 and ops[1][0] == 'ccr' and ops[0][0] == 'imm':
            S('L_ccr_op(%d, 0x%xu);' % ({'andi': 0, 'ori': 1, 'eori': 2}[mn], ops[0][1] & 0x1F))
            return done()
        if mn in ('andi', 'ori', 'eori') and len(ops) == 2 and ops[1][0] == 'sr' and ops[0][0] == 'imm':
            S('lift_set_sr(lift_get_sr() %s 0x%xu);' % ({'andi': '&', 'ori': '|', 'eori': '^'}[mn], ops[0][1] & 0xFFFF))
            return done()
        if mn in ('addx', 'subx') and len(ops) == 2:
            if ops[0][0] == 'd' and ops[1][0] == 'd':
                S('D[%d] = (D[%d] & ~MK(%d)) | L_%s(D[%d], D[%d], %d);' % (ops[1][1], ops[1][1], sz, mn, ops[0][1], ops[1][1], sz))
                return done()
            if ops[0][0] == 'pre' and ops[1][0] == 'pre':
                a, b = ops[0][1], ops[1][1]
                S('{ A[%d] -= %d; u32 s_ = m%d(A[%d]); A[%d] -= %d; u32 d_ = m%d(A[%d]); w%d(A[%d], L_%s(s_, d_, %d)); }' % (a, sz, 8 * sz, a, b, sz, 8 * sz, b, 8 * sz, b, mn, sz))
                return done()
            raise NoLift('addx form')
        if mn in ('add', 'sub', 'and', 'or', 'eor', 'cmp', 'addi', 'subi', 'andi', 'ori', 'eori', 'cmpi'):
            base = mn[:-1] if mn.endswith('i') and mn != 'ori' or mn == 'ori' else mn
            base = {'addi': 'add', 'subi': 'sub', 'andi': 'and', 'ori': 'or', 'eori': 'eor', 'cmpi': 'cmp'}.get(mn, mn)
            src, dst = ops[0], ops[1]
            s = self.read(src, sz, I, ctx)
            if base == 'cmp':
                d = self.read(dst, sz, I, ctx)
                S('L_cmp(%s, %s, %d);' % (s, d, sz))
                return done()
            fnn = {'add': 'L_add', 'sub': 'L_sub'}.get(base)
            if dst[0] in ('d', 'a'):
                d = self.read(dst, sz, I, ctx)
                if fnn: expr = '%s(%s, %s, %d)' % (fnn, s, d, sz)
                else: expr = 'L_logic(%s %s %s, %d)' % (d, {'and': '&', 'or': '|', 'eor': '^'}[base], s, sz)
                S('{ u32 r_ = %s;' % ('(u32)(%s)' % expr))
                S(self.write(dst, sz, 'r_', I, ctx))
                S('}')
            else:
                pre, a = self.ea_expr(dst, sz, I, ctx, 'ed')
                ctx['pre'] += pre
                S('{ u32 ad_ = %s; u32 r_ = %s;' % (a, ('%s(%s, m%d(ad_), %d)' % (fnn, s, 8 * sz, sz)) if fnn else 'L_logic(m%d(ad_) %s %s, %d)' % (8 * sz, {'and': '&', 'or': '|', 'eor': '^'}[base], s, sz)))
                S('w%d(ad_, r_); }' % (8 * sz))
            return done()
        if mn in ('adda', 'suba', 'cmpa'):
            s = self.read(ops[0], sz, I, ctx)
            if sz == 2: s = self.sxv(s, 2)
            an = ops[1][1]
            if mn == 'adda': S('A[%d] += %s;' % (an, s))
            elif mn == 'suba': S('A[%d] -= %s;' % (an, s))
            else: S('L_cmp(%s, A[%d], 4);' % (s, an))
            return done()
        if mn in ('addq', 'subq'):
            v = ops[0][1] & 0xFF or 8
            dst = ops[1]
            if dst[0] == 'a':
                S('A[%d] %s= %d;' % (dst[1], '+' if mn == 'addq' else '-', v))
                return done()
            fnn = 'L_add' if mn == 'addq' else 'L_sub'
            if dst[0] == 'd':
                d = self.read(dst, sz, I, ctx)
                S('{ u32 r_ = %s(%d, %s, %d);' % (fnn, v, d, sz))
                S(self.write(dst, sz, 'r_', I, ctx))
                S('}')
            else:
                pre, a = self.ea_expr(dst, sz, I, ctx, 'ed')
                ctx['pre'] += pre
                S('{ u32 ad_ = %s; w%d(ad_, %s(%d, m%d(ad_), %d)); }' % (a, 8 * sz, fnn, v, 8 * sz, sz))
            return done()
        if mn in ('neg', 'not', 'negx'):
            dst = ops[0]
            if dst[0] == 'd':
                d = self.read(dst, sz, I, ctx)
                f = {'neg': 'L_neg(%s, %d)', 'not': 'L_logic(~%s, %d)', 'negx': 'L_negx(%s, %d)'}[mn] % (d, sz)
                S('{ u32 r_ = %s;' % f)
                S(self.write(dst, sz, 'r_', I, ctx))
                S('}')
            else:
                pre, a = self.ea_expr(dst, sz, I, ctx, 'ed')
                ctx['pre'] += pre
                f = {'neg': 'L_neg(m%d(ad_), %d)', 'not': 'L_logic(~m%d(ad_), %d)', 'negx': 'L_negx(m%d(ad_), %d)'}[mn] % (8 * sz, sz)
                S('{ u32 ad_ = %s; w%d(ad_, %s); }' % (a, 8 * sz, f))
            return done()
        if mn == 'ext':
            n = ops[0][1]
            if sz == 2: S('D[%d] = (D[%d]&0xFFFF0000u)|((u32)(s32)(s8)D[%d]&0xFFFFu); LNZ(D[%d], 2); fV = fC = 0;' % (n, n, n, n))
            else: S('D[%d] = (u32)(s32)(s16)D[%d]; LNZ(D[%d], 4); fV = fC = 0;' % (n, n, n))
            return done()
        if mn == 'extb':
            n = ops[0][1]
            S('D[%d] = (u32)(s32)(s8)D[%d]; LNZ(D[%d], 4); fV = fC = 0;' % (n, n, n))
            return done()
        if mn == 'swap':
            n = ops[0][1]
            S('D[%d] = (D[%d]<<16)|(D[%d]>>16); LNZ(D[%d], 4); fV = fC = 0;' % (n, n, n, n))
            return done()
        if mn == 'exg':
            a, b = ops
            ra = ('D[%d]' if a[0] == 'd' else 'A[%d]') % a[1]
            rb = ('D[%d]' if b[0] == 'd' else 'A[%d]') % b[1]
            S('{ u32 t_ = %s; %s = %s; %s = t_; }' % (ra, ra, rb, rb))
            return done()
        if mn in ('mulu', 'muls', 'divu', 'divs'):
            if ops[1][0] != 'd': raise NoLift('mul/div 64-bit forms')
            dn = ops[1][1]
            s = self.read(ops[0], sz, I, ctx)
            if sz == 2:
                if mn in ('mulu', 'muls'):
                    S('D[%d] = L_%s16(%s, D[%d]);' % (dn, mn, s, dn))
                else:
                    S('{ u32 q_ = 0; int e_ = L_%s16(%s, D[%d], &q_); if (e_ == 1) LBAIL(%s); else if (!e_) D[%d] = q_; }' % (mn, s, dn, self.ra(I.addr), dn))
            else:
                ew, is64 = I.extra
                dh = ew & 7
                if mn in ('mulu', 'muls'):
                    S('D[%d] = L_%s32(%s, D[%d]);' % (dn, mn, s, dn))
                else:
                    S('{ u32 q_ = 0, r_ = 0; int e_ = L_%s32(%s, D[%d], &q_, &r_); if (e_ == 1) LBAIL(%s); else if (!e_) { %s D[%d] = q_; } }' % (mn, s, dn, self.ra(I.addr), ('D[%d] = r_;' % dh) if dh != dn else '', dn))
            return done()
        if mn in ('lsl', 'lsr', 'asl', 'asr', 'rol', 'ror', 'roxl', 'roxr'):
            if len(ops) == 1:
                pre, a = self.ea_expr(ops[0], 2, I, ctx, 'ed')
                ctx['pre'] += pre
                S('{ u32 ad_ = %s; w16(ad_, L_%s(m16(ad_), 1, 2)); }' % (a, mn))
                return done()
            cnt, dst = ops[0], ops[1]
            if dst[0] != 'd': raise NoLift('shift mem')
            c = ('%d' % (cnt[1] if cnt[1] else 8)) if cnt[0] == 'imm' else '(D[%d]&63u)' % cnt[1]
            S('{ u32 r_ = L_%s(D[%d], %s, %d);' % (mn, dst[1], c, sz))
            S(self.write(dst, sz, 'r_', I, ctx))
            S('}')
            return done()
        if mn in ('btst', 'bset', 'bclr', 'bchg'):
            bit, dst = ops[0], ops[1]
            b = ('%d' % bit[1]) if bit[0] == 'imm' else 'D[%d]' % bit[1]
            if dst[0] == 'd':
                S('{ u32 m_ = 1u << ((%s)&31u); fZ = (D[%d]&m_) == 0;' % (b, dst[1]))
                if mn == 'bset': S('D[%d] |= m_;' % dst[1])
                elif mn == 'bclr': S('D[%d] &= ~m_;' % dst[1])
                elif mn == 'bchg': S('D[%d] ^= m_;' % dst[1])
                S('}')
            else:
                pre, a = self.ea_expr(dst, 1, I, ctx, 'ed')
                ctx['pre'] += pre
                S('{ u32 ad_ = %s; u32 m_ = 1u << ((%s)&7u); u32 v_ = m8(ad_); fZ = (v_&m_) == 0;' % (a, b))
                if mn == 'bset': S('w8(ad_, v_|m_);')
                elif mn == 'bclr': S('w8(ad_, v_&~m_);')
                elif mn == 'bchg': S('w8(ad_, v_^m_);')
                S('}')
            return done()
        if mn[0] == 's' and mn[1:] in CONDS and mn not in ('sub', 'suba', 'subi', 'subq', 'swap', 'subx'):
            c = CONDS[mn[1:]]
            S(self.write(ops[0], 1, '((%s) ? 0xFFu : 0u)' % c, I, ctx))
            return done()
        if mn == 'cmpm':
            S('{ u32 s_ = m%d(A[%d]); A[%d] += %d; u32 d_ = m%d(A[%d]); A[%d] += %d; L_cmp(s_, d_, %d); }' % (8 * sz, ops[0][1], ops[0][1], sz, 8 * sz, ops[1][1], ops[1][1], sz, sz))
            return done()
        if mn == 'movem':
            a, b = ops
            if a[0] == 'regs':      # registers -> memory
                mask, predec = a[1], a[2]
                if b[0] == 'pre':
                    n = b[1]
                    names = ['A[7]', 'A[6]', 'A[5]', 'A[4]', 'A[3]', 'A[2]', 'A[1]', 'A[0]', 'D[7]', 'D[6]', 'D[5]', 'D[4]', 'D[3]', 'D[2]', 'D[1]', 'D[0]']
                    S('{ u32 p_ = A[%d];' % n)
                    for i in range(16):
                        if mask >> i & 1:
                            S('p_ -= %d; w%d(p_, %s);' % (sz, 8 * sz, names[i]))
                    S('A[%d] = p_; }' % n)
                else:
                    pre, ad = self.ea_expr(b, sz, I, ctx)
                    ctx['pre'] += pre
                    names = ['D[%d]' % i for i in range(8)] + ['A[%d]' % i for i in range(8)]
                    S('{ u32 p_ = %s;' % ad)
                    for i in range(16):
                        if mask >> i & 1:
                            S('w%d(p_, %s); p_ += %d;' % (8 * sz, names[i], sz))
                    S('}')
            else:                   # memory -> registers
                mask = b[1]
                names = ['D[%d]' % i for i in range(8)] + ['A[%d]' % i for i in range(8)]
                if a[0] == 'post':
                    n = a[1]
                    S('{ u32 p_ = A[%d];' % n)
                else:
                    pre, ad = self.ea_expr(a, sz, I, ctx)
                    ctx['pre'] += pre
                    S('{ u32 p_ = %s;' % ad)
                for i in range(16):
                    if mask >> i & 1:
                        v = 'm%d(p_)' % (8 * sz)
                        if sz == 2: v = '(u32)(s32)(s16)' + v
                        S('%s = %s; p_ += %d;' % (names[i], v, sz))
                if a[0] == 'post': S('A[%d] = p_;' % a[1])
                S('}')
            return done()
        if mn in ('bfextu', 'bfexts', 'bfffo', 'bfins', 'bfclr', 'bfset', 'bfchg', 'bftst'):
            return self.bitfield(I, ctx, out, mn, ops)
        raise NoLift('mnemonic ' + mn)

    def move_fix(self, ctx, out):
        return ctx['pre'] + out

    def bitfield(self, I, ctx, out, mn, ops):
        S = out.append
        if mn == 'bfins': bf, reg = ops[1], ops[0]
        elif mn in ('bfextu', 'bfexts', 'bfffo'): bf, reg = ops[0], ops[1]
        else: bf, reg = ops[0], None
        _, ea, off, wid = bf
        offs = 'D[%d]' % off[1] if isinstance(off, tuple) else '%d' % off
        wids = '(D[%d]&31u)' % wid[1] if isinstance(wid, tuple) else '%d' % wid
        wids = '((%s)?(%s):32u)' % (wids, wids) if isinstance(wid, tuple) else ('%d' % (wid or 32))
        opn = {'bfextu': 'BF_EXTU', 'bfexts': 'BF_EXTS', 'bfffo': 'BF_FFO', 'bfins': 'BF_INS', 'bfclr': 'BF_CLR', 'bfset': 'BF_SET', 'bfchg': 'BF_CHG', 'bftst': 'BF_TST'}[mn]
        if ea[0] == 'd':
            tgt = 'BFREG(D[%d])' % ea[1]
            S('{ u32 f_ = BF_GET_D(D[%d], %s, %s); ' % (ea[1], offs, wids))
            S('LBF_FLAGS(f_, %s);' % wids)
            if mn in ('bfextu',): S('D[%d] = f_; }' % reg[1])
            elif mn == 'bfexts': S('D[%d] = (u32)((s32)(f_ << (32-%s)) >> (32-%s)); }' % (reg[1], wids, wids))
            elif mn == 'bfffo': S('D[%d] = BF_FFO_RES(f_, %s, %s); }' % (reg[1], offs, wids))
            elif mn == 'bfins':
                S('BF_PUT_D(&D[%d], %s, %s, D[%d]); LBF_FLAGS(D[%d]&BF_MASK(%s), %s); }' % (ea[1], offs, wids, reg[1], reg[1], wids, wids))
            elif mn == 'bfclr': S('BF_PUT_D(&D[%d], %s, %s, 0); }' % (ea[1], offs, wids))
            elif mn == 'bfset': S('BF_PUT_D(&D[%d], %s, %s, 0xFFFFFFFFu); }' % (ea[1], offs, wids))
            elif mn == 'bfchg': S('BF_PUT_D(&D[%d], %s, %s, ~f_); }' % (ea[1], offs, wids))
            else: S('}')
        else:
            pre, a = self.ea_expr(ea, 1, I, ctx)
            ctx['pre'] += pre
            S('{ u32 ba_ = %s; s32 bo_ = (s32)(%s); u32 bw_ = %s; u32 f_ = BF_GET_M(ba_, bo_, bw_);' % (a, offs, wids))
            S('LBF_FLAGS(f_, bw_);')
            if mn == 'bfextu': S('D[%d] = f_; }' % reg[1])
            elif mn == 'bfexts': S('D[%d] = (u32)((s32)(f_ << (32-bw_)) >> (32-bw_)); }' % reg[1])
            elif mn == 'bfffo': S('D[%d] = BF_FFO_RES(f_, (u32)bo_, bw_); }' % reg[1])
            elif mn == 'bfins': S('BF_PUT_M(ba_, bo_, bw_, D[%d]); LBF_FLAGS(D[%d]&BF_MASK(bw_), bw_); }' % (reg[1], reg[1]))
            elif mn == 'bfclr': S('BF_PUT_M(ba_, bo_, bw_, 0); }')
            elif mn == 'bfset': S('BF_PUT_M(ba_, bo_, bw_, 0xFFFFFFFFu); }')
            elif mn == 'bfchg': S('BF_PUT_M(ba_, bo_, bw_, ~f_); }')
            else: S('}')
        return ctx['pre'] + out

    def fname(self, addr):
        sid, off = self.seg_off(addr)
        return 'L_%d_%x' % (sid, off)

    # ---- function ----
    def function(self, entry, info, entries):
        P = self.P
        addrs = sorted(set(info['insts']))
        fset = set(addrs)
        insts = {}
        for a in addrs:
            insts[a] = analyze.fetch(P, a)
        labels = set()
        body = []
        order = addrs
        bails = 0
        for idx, a in enumerate(order):
            I = insts[a]
            try:
                code = self.inst(I, entry, fset, labels, entries)
            except (NoLift, KeyError, IndexError, TypeError) as e:
                code = ['LBAIL(%s); /* unsupported: %s %s */' % (self.ra(a), I.text().replace('*/', '* /'), e)]
                bails += 1
            term = I.mn in ('rts', 'rtd', 'bra', 'jmp') or (I.mn == 'rtd')
            line = ['L%x: ; /* %s */' % (a, I.text().replace('*/', '* /'))]
            body.append((a, line, code, term, I))
        # fallthrough handling
        outl = []
        for idx, (a, line, code, term, I) in enumerate(body):
            outl.append('{')
            outl += ['  ' + x for x in code] if False else code
            outl.append('}')
        return addrs, body, fset, labels, bails

    def emit_function(self, entry, info, entries):
        addrs, body, fset, labels, bails = self.function(entry, info, entries)
        sid, off = self.seg_off(entry)
        ft = {}
        for k, (a, line, code, term, I) in enumerate(body):
            if not term:
                nxt = a + I.size
                if not (k + 1 < len(body) and body[k + 1][0] == nxt):
                    ft[a] = nxt
                    if nxt in fset: labels.add(nxt)
        mids = []
        for k, (a, line, code, term, I) in enumerate(body):
            if I.mn == 'aline': cand = [a, a + I.size]
            elif I.mn in ('jsr', 'bsr'): cand = [a + I.size]
            else: cand = []
            for c in cand:
                if c in fset and c != entry and c not in mids: mids.append(c)
        for c in mids: labels.add(c)
        self.mids = getattr(self, 'mids', [])
        haslink = 1 if body and body[0][0] == entry and body[0][4].mn == 'link' else 0
        for c in mids: self.mids.append((sid, c - analyze.SEGBASE(sid), self.fname(entry), haslink))
        o = ['/* seg %d +0x%x */' % (sid, off), 'void %s(void) {' % self.fname(entry),
             '  u32 ea = 0, ed = 0; u32 mid_ = lift_mid; const u32 R0 = mid_ ? lift_mid_r0 : m32(A[7]); lift_mid = 0; (void)ea; (void)ed; (void)R0;',
             '  LCOV(%d);' % (off >> 1)]
        if mids:
            o.append('  if (mid_) switch (mid_) {')
            for c in mids: o.append('    case 0x%x: goto L%x;' % (c - analyze.SEGBASE(sid), c))
            o.append('    default: lift_bail(SB[LSEG] + mid_); }')
        if body and body[0][0] != entry:
            labels.add(entry); o.append('  goto L%x;' % entry)
        for k, (a, line, code, term, I) in enumerate(body):
            lab = ('L%x: ; ' % a) if a in labels else ''
            o.append('  %s/* %x %s */' % (lab, a, I.text().replace('*/', '* /')))
            o.append('  LTR(0x%x);' % a)
            o.append('  {')
            for c in code: o.append('  ' + c)
            o.append('  }')
            if a in ft:
                nxt = ft[a]
                o.append('  goto L%x;' % nxt if nxt in fset else '  LTAIL(%s);' % self.ra(nxt))
        o.append('}')
        return '\n'.join(o), bails


def main():
    only = [int(x) for x in sys.argv[1:]] if len(sys.argv) > 1 else None
    P = analyze.build()
    analyze.analyze(P)
    relocs = json.load(open(os.path.join(ROOT, 'gen', 'relocs.json')))
    for _s in (4, 12, 14, 15): relocs.setdefault(str(_s), [])
    G = Gen(P, relocs); G.mids = []
    entries = set(P.funcs)
    os.makedirs(OUT, exist_ok=True)
    byseg = collections.defaultdict(list)
    for e in sorted(P.funcs):
        so = G.seg_off(e)
        if so is None: continue
        byseg[so[0]].append(e)
    tabs = []
    tot = bails = 0
    protos = []
    for sid in sorted(byseg):
        if only and sid not in only: continue
        if sid not in G.relocs: print('skipping seg %d (no relocation data)' % sid, file=sys.stderr); rm = os.path.join(OUT, 'lift_seg%d.c' % sid); (os.remove(rm) if os.path.exists(rm) else None); continue
        parts = ['#define LSEG %d' % sid, '#include "../liftrt.h"', '#include "lift_protos.h"', '/* generated by tools/lift.py -- do not edit */']
        for e in byseg[sid]:
            try:
                t, b = G.emit_function(e, P.funcs[e], entries)
            except Exception as ex:
                print('FAILED function %x: %r' % (e, ex), file=sys.stderr)
                continue
            parts.append(t)
            tot += 1; bails += b
            protos.append('void %s(void);' % G.fname(e))
            tabs.append((sid, e - analyze.SEGBASE(sid), G.fname(e)))
        open(os.path.join(OUT, 'lift_seg%d.c' % sid), 'w').write('\n'.join(parts) + '\n')
    # prototypes + table (only when generating everything)
    if not only:
        open(os.path.join(OUT, 'lift_protos.h'), 'w').write('#include "../liftrt.h"\n' + '\n'.join(protos) + '\n')
        with open(os.path.join(OUT, 'lift_tab.c'), 'w') as f:
            f.write('#include "../liftrt.h"\n#include "lift_protos.h"\n')
            f.write('const LiftEnt lift_ents[] = {\n')
            for sid, off, n in tabs: f.write('  {%d, 0x%x, %s},\n' % (sid, off, n))
            f.write('};\nconst int lift_nents = %d;\n' % len(tabs))
            f.write('const LiftMid lift_mids[] = {\n')
            for sid, off, n, hl in G.mids: f.write('  {%d, 0x%x, %s, %d},\n' % (sid, off, n, hl))
            f.write('};\nconst int lift_nmids = %d;\n' % len(G.mids))
            import mdrv
            if os.path.exists(os.path.join(ROOT, 'extracted/app/MDRV/11.bin')):
                _md = mdrv.decode()
                f.write('const unsigned char lift_mdrv_sig[16] = {%s};\nconst unsigned lift_mdrv_len = %d;\n' % (','.join(str(b) for b in _md[:16]), len(_md)))
            else: f.write('const unsigned char lift_mdrv_sig[16] = {0};\nconst unsigned lift_mdrv_len = 0;\n')
            _sm = open(os.path.join(ROOT, 'extracted/app/SMOD/0.bin'), 'rb').read() if os.path.exists(os.path.join(ROOT, 'extracted/app/SMOD/0.bin')) else b''
            f.write('const unsigned char lift_smod_sig[16] = {%s};\nconst unsigned lift_smod_len = %d;\n' % (','.join(str(b) for b in (_sm[:16] or bytes(16))), len(_sm)))
    print('functions: %d, instructions bailed: %d' % (tot, bails))


if __name__ == '__main__':
    main()
