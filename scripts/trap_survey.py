import sys, os, struct, collections, glob
traps = collections.Counter()
per = {}
total_ins = 0
for f in sorted(glob.glob('work/code/CODE_*.bin'), key=lambda p:int(p.split('_')[1].split('.')[0])):
    rid = int(f.split('_')[1].split('.')[0])
    if rid in (0,): continue
    d = open(f,'rb').read()[4:]   # skip 4-byte segment header (jt offset, entries)
    n = 0
    for i in range(0, len(d)-1, 2):
        w = struct.unpack('>H', d[i:i+2])[0]
        if (w & 0xF000) == 0xA000:
            traps[w] += 1
    per[rid] = len(d)
print('distinct A-trap words:', len(traps), ' total A-trap words:', sum(traps.values()))
for w, c in traps.most_common(60):
    print(f'{w:04X} x{c}', end=' | ')
print()
