import sys, os, struct
sys.path.insert(0, '/home/claude/castles-decomp/tools/rsrcfork/src')
import rsrcfork
rf = rsrcfork.open('work/castles/rsrc.fork', fork='data')
os.makedirs('work/code', exist_ok=True)
for rid, r in sorted(rf[b'CODE'].items()):
    open(f'work/code/CODE_{rid}.bin', 'wb').write(r.data)
    print(rid, len(r.data), r.name, r.attributes, r.data[:8].hex())
c0 = rf[b'CODE'][0].data if 0 in rf[b'CODE'] else None
if c0:
    above, below, jtlen, joff = struct.unpack('>IIII', c0[:16])
    print('CODE0 above/below/jtlen/joff', above, below, jtlen, joff)
