import yaml, glob, json, re, collections
traps = {}
for f in glob.glob('tools/multiversal/defs/*.yaml'):
    try: doc = yaml.safe_load(open(f))
    except Exception as e: continue
    if not isinstance(doc, list): continue
    for it in doc:
        fn = it.get('function') if isinstance(it, dict) else None
        if not fn or 'trap' not in fn: continue
        t = fn['trap']
        if isinstance(t, str): t = int(t, 16) if t.lower().startswith('0x') else None
        if t is None: continue
        args = [(a.get('name'), str(a.get('type')), a.get('register')) for a in fn.get('args', [])]
        traps.setdefault(t, (fn['name'], str(fn.get('return', 'void')), args, f.split('/')[-1][:-5]))
print(len(traps), 'traps with explicit numbers')
json.dump({hex(k): v for k, v in traps.items()}, open('work/traps.json', 'w'), indent=0)
# survey: which of the game's trap words are known
import struct, glob as g
used = collections.Counter()
for p in g.glob('work/code/CODE_*.bin'):
    if p.endswith('_0.bin'): continue
    d = open(p,'rb').read()[4:]
    for i in range(0, len(d)-1, 2):
        w = struct.unpack('>H', d[i:i+2])[0]
        if (w & 0xF000) == 0xA000: used[w] += 1
known = {w: traps[w] for w in used if w in traps}
print('used words', len(used), 'known by name', len(known))
by_mgr = collections.Counter(v[3] for v in known.values())
print(by_mgr.most_common(20))
for w, c in used.most_common(40):
    print(f'{w:04X}', c, traps.get(w, ('?',))[0], end=' | ')
