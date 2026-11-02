#!/usr/bin/env python3
"""
Recursive-descent analysis of all CODE segments.

Guest layout used throughout the project:
    segment N  is loaded at  SEGBASE(N) = 0x00100000 + N*0x10000   (resource bytes, incl. 4-byte header)
    code offset X in a jump-table entry  ->  SEGBASE(N) + 4 + X
"""
import sys, os, struct, collections, pickle
sys.path.insert(0, os.path.dirname(__file__))
import m68k, segs

def SEGBASE(n): return 0x00100000 + n * 0x10000

class Prog:
    pass

# MPW runtime switch helpers living in segment 1 (found by inspection)
SWITCH_WORD  = SEGBASE(1) + 0x148   # pairs (off.w, val.w)
SWITCH_LONG  = SEGBASE(1) + 0x15c   # pairs (off.w, val.l)
SWITCH_RANGE = SEGBASE(1) + 0x170   # lo.w hi.w default.w off[]...

def build():
    M = segs.load()
    P = Prog()
    P.M = M
    P.img = {}       # guest addr range -> bytes ; keyed by seg id
    P.segbase = {sid: SEGBASE(sid) for sid in M['segs']}
    P.mem = {}       # sid -> bytes
    for sid, s in M['segs'].items():
        P.mem[sid] = s.data
    P.jt = {}        # jt index -> guest target address
    P.jt_a5off = {}  # a5 offset of JMP word -> target
    for idx, sid, off in M['entries']:
        tgt = SEGBASE(sid) + 4 + off
        P.jt[idx] = tgt
        P.jt_a5off[0x20 + idx * 8 + 2] = tgt
    return P

def seg_of(P, addr):
    for sid in P.mem:
        b = SEGBASE(sid)
        if b <= addr < b + len(P.mem[sid]):
            return sid
    return None

def fetch(P, addr):
    sid = seg_of(P, addr)
    if sid is None: return None
    return m68k.decode(P.mem[sid], addr - SEGBASE(sid), SEGBASE(sid))

def jump_table(P, hist):
    """Recognise  [cmpi.w #N,dX; bhi]  lea T(pc),aK ; adda.w 0(aK,dX.w*2),aK ; jmp (aK)  -> list of target addresses (else None)."""
    import re
    if len(hist) < 3: return None
    j, ad, le = hist[-1], hist[-2], hist[-3]
    mj = re.match(r'jmp\s+\(a(\d)\)$', j.text())
    ma = re.match(r'adda\.w\s+\$0\(a(\d),d(\d)\.w\*2\),a(\d)$', ad.text())
    ml = re.match(r'lea\.l\s+\S+\(pc\),a(\d)$', le.text())
    if not (mj and ma and ml): return None
    k = mj.group(1)
    if not (ma.group(1) == k and ma.group(3) == k and ml.group(1) == k): return None
    dreg = ma.group(2); n = None
    for h in reversed(hist[:-3][-8:]):
        m = re.match(r'cmpi\.w\s+#\$?(-?[0-9a-f]+),d(\d)$', h.text())
        if m and m.group(2) == dreg:
            n = int(m.group(1), 16); break
    if n is None or n > 256: return None
    o = le.ops[0]
    if not (isinstance(o, tuple) and o[0] == 'pcd'): return None
    T = o[2]; sid = seg_of(P, T)
    if sid is None: return None
    mem = P.mem[sid]; base = SEGBASE(sid); out = []
    for idx in range(n + 1):
        off = T - base + 2 * idx
        if off + 2 > len(mem): return None
        w = struct.unpack_from('>h', mem, off)[0]
        t = T + w
        if seg_of(P, t) != sid: return None
        out.append(t)
    return out

def _analyze1(P, extra=(), verbose=False):
    funcs = {}        # entry addr -> info
    insts = {}        # addr -> Inst
    switch_sites = {} # addr of the jsr -> dict(kind, table_addr, targets, end)
    warnings = []
    work = collections.deque()
    entries = set(P.jt.values())
    entries.add(SEGBASE(1) + 0x10)
    if 14 in P.mem: entries.add(SEGBASE(14))
    if 15 in P.mem: entries.add(SEGBASE(15))
    entries |= set(extra)
    for a in entries: work.append(a)
    seen_entry = set()
    traps = collections.Counter()
    trap_sites = collections.defaultdict(list)
    callgraph = collections.defaultdict(set)
    ind_calls = []
    ind_jumps = []
    jmp_tables = {}

    def parse_switch(jsr_addr, tab, kind):
        sid = seg_of(P, tab); base = SEGBASE(sid); m = P.mem[sid]
        o = tab - base
        tg = []
        if kind == 'word' or kind == 'long':
            n = struct.unpack_from('>H', m, o)[0] + 1; o2 = o + 2
            esz = 4 if kind == 'word' else 6
            for k in range(n):
                p = o2 + k * esz
                off = struct.unpack_from('>h', m, p)[0]
                if kind == 'word': val = struct.unpack_from('>H', m, p + 2)[0]
                else: val = struct.unpack_from('>I', m, p + 2)[0]
                tg.append((val, base + p + off))
            end = base + o2 + n * esz
            return dict(kind=kind, table=tab, cases=tg, default=tg[-1][1], end=end)
        else:
            lo, hi = struct.unpack_from('>HH', m, o)
            lo = lo if lo < 0x8000 else lo - 0x10000
            hi = hi if hi < 0x8000 else hi - 0x10000
            t0 = o + 4
            doff = struct.unpack_from('>h', m, t0)[0]
            default = base + t0 + doff
            for k in range(hi - lo + 1):
                p = t0 + 2 + 2 * k
                off = struct.unpack_from('>h', m, p)[0]
                tg.append((lo + k, base + p + off))
            end = base + t0 + 2 + 2 * (hi - lo + 1)
            return dict(kind='range', table=tab, cases=tg, default=default, end=end, lo=lo, hi=hi)

    while work:
        entry = work.popleft()
        if entry in seen_entry: continue
        seen_entry.add(entry)
        finfo = dict(entry=entry, blocks=set(), insts=[], calls=set(), tail=set())
        funcs[entry] = finfo
        stack = [entry]
        visited = set()
        while stack:
            a = stack.pop(); hist = []
            while True:
                if a in visited: break
                try:
                    i = fetch(P, a)
                except m68k.DecodeError as e:
                    warnings.append(('decode', a, str(e))); break
                if i is None:
                    warnings.append(('outside', a)); break
                visited.add(a); insts[a] = i; hist.append(i)
                finfo['insts'].append(a)
                mn = i.mn; nxt = a + i.size
                if mn == 'aline':
                    tw = i.ops[0][1]
                    traps[tw] += 1; trap_sites[tw].append(a)
                    a = nxt; continue
                if mn in ('bra',) :
                    t = i.ops[0][1]
                    if t in P.jt.values() or (t in funcs) : finfo['tail'].add(t); work.append(t); break
                    stack.append(t); break
                if mn.startswith('b') and mn != 'bsr' and mn[1:] in m68k.COND:
                    stack.append(i.ops[0][1]); a = nxt; continue
                if mn.startswith('db'):
                    stack.append(i.ops[1][1]); a = nxt; continue
                if mn == 'bsr':
                    t = i.ops[0][1]; work.append(t); finfo['calls'].add(t); a = nxt; continue
                if mn == 'jsr':
                    o = i.ops[0]
                    tgt = None
                    if o[0] == 'pcd': tgt = o[2]
                    elif o[0] == 'absl': tgt = o[1]
                    elif o[0] == 'disp' and o[1] == 5:
                        tgt = P.jt_a5off.get(o[2])
                        if tgt is None: warnings.append(('jsr a5 disp not in jt', a, o))
                    if tgt is not None:
                        if tgt in (SWITCH_WORD, SWITCH_LONG, SWITCH_RANGE):
                            kind = {SWITCH_WORD: 'word', SWITCH_LONG: 'long', SWITCH_RANGE: 'range'}[tgt]
                            sw = parse_switch(a, nxt, kind)
                            switch_sites[a] = sw
                            for _, t in sw['cases']: stack.append(t)
                            stack.append(sw['default'])
                            break
                        work.append(tgt); finfo['calls'].add(tgt); a = nxt; continue
                    ind_calls.append((a, i.text())); a = nxt; continue
                if mn == 'jmp':
                    o = i.ops[0]
                    tg = jump_table(P, hist)
                    if tg:
                        jmp_tables[a] = tg
                        for t in tg: stack.append(t)
                        break
                    ind_jumps.append((a, i.text())); break
                if mn in ('rts', 'rte', 'rtr', 'rtd'): break
                if mn == 'illegal': break
                a = nxt
        callgraph[entry] = finfo['calls']
    P.funcs = funcs; P.insts = insts; P.switch_sites = switch_sites; P.jmp_tables = jmp_tables
    P.traps = traps; P.trap_sites = trap_sites
    P.warnings = warnings; P.ind_calls = ind_calls; P.ind_jumps = ind_jumps
    return P

MDRV_CODE_END = 0x4200      # music driver: code ends here, the rest is its state/tables

def find_extra(P):
    ex = set()
    for a, i in P.insts.items():
        if i.mn in ('pea', 'lea'):
            for op in i.ops:
                if isinstance(op, tuple) and op[0] == 'pcd' and op[2] not in P.funcs and op[2] not in P.insts:
                    try:
                        j = fetch(P, op[2])
                    except Exception: j = None
                    if j is not None and not (seg_of(P, op[2]) == 14 and op[2] - SEGBASE(14) >= MDRV_CODE_END): ex.add(op[2])
    for sid, m in P.mem.items():
        base = SEGBASE(sid)
        for o in range(0, len(m) - 4, 2):
            a = base + o
            if a not in P.insts and struct.unpack_from('>H', m, o)[0] == 0x4E56: ex.add(a)
    import os
    fn = os.path.join(os.path.dirname(__file__), '..', 'gen', 'entries_extra.txt')
    if os.path.exists(fn):
        for l in open(fn):
            l = l.strip()
            if ':' in l:
                sg, of = l.split(':'); a = SEGBASE(int(sg)) + int(of, 16)
                if a not in P.funcs and sg and int(sg) in P.mem:
                    try: j = fetch(P, a)
                    except Exception: j = None
                    if j is not None: ex.add(a)
    return ex

def analyze(P, verbose=False):
    extra = set()
    for _ in range(6):
        _analyze1(P, extra, verbose)
        new = find_extra(P) - extra
        if not new: break
        extra |= new
    return P

if __name__ == '__main__':
    P = build()
    analyze(P)
    print('functions:', len(P.funcs), ' instructions reached:', len(P.insts), ' switch sites:', len(P.switch_sites))
    tot = sum(len(P.mem[s]) for s in P.mem)
    covered = sum(i.size for i in P.insts.values())
    print('code bytes total', tot, ' covered by instructions', covered)
    print('warnings:', len(P.warnings))
    for w in P.warnings[:30]: print('  ', w)
    print('indirect calls:', len(P.ind_calls))
    c = collections.Counter(t for _, t in P.ind_calls)
    for t, n in c.most_common(25): print('   %4d  %s' % (n, t))
    print('indirect jumps:', len(P.ind_jumps))
    for a, t in P.ind_jumps[:40]: print('   %08x %s' % (a, t))
    pickle.dump(None, open('/dev/null', 'wb'))
