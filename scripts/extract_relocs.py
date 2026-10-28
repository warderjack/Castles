#!/usr/bin/env python3
"""Find load-time relocations in CODE segments by diffing resource bytes against a RAM dump.
usage: extract_relocs.py ram.bin run.log out.json   (run.log must contain '[seg] LoadSeg(n) base=XXXXXX' lines)"""
import sys, re, json, struct
sys.path.insert(0, '/home/claude/castles-decomp/tools')
import segs
M = segs.load()
ram = open(sys.argv[1], 'rb').read()
bases = {int(m.group(1)): int(m.group(2), 16) for m in re.finditer(r'\[seg\] LoadSeg\((\d+)\) base=([0-9a-f]+)', open(sys.argv[2], errors='replace').read())}
A4, A5 = 0x50434, 0xC07532
out = {}
for sid, b in sorted(bases.items()):
    d = M['segs'][sid].data; r = ram[b:b + len(d)]; lst = []
    for o in range(4, len(d) - 3, 2):
        old = struct.unpack_from('>I', d, o)[0]; new = struct.unpack_from('>I', r, o)[0]
        if old != new:
            dl = (new - old) & 0xFFFFFFFF
            if dl == A4: lst.append([o, 'A4'])
            elif dl == A5: lst.append([o, 'A5'])
    out[sid] = lst
    print(sid, hex(b), len(lst), 'relocs; other diffs:', sum(1 for i in range(len(d)) if d[i] != r[i]))
json.dump(out, open(sys.argv[3], 'w'))
