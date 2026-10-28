import sys, struct
sys.path.insert(0, '/home/claude/castles-decomp/tools/rsrcfork/src')
import rsrcfork
rf = rsrcfork.open('work/castles/rsrc.fork', fork='data')
def hx(b, n=96):
    return ' '.join(f'{x:02x}' for x in b[:n])
for t in (b'DATA', b'ZERO', b'DREL'):
    d = rf[t][0].data
    print(t, len(d)); print(hx(d, 64))
for rid, r in sorted(rf[b'CREL'].items()):
    print('CREL', rid, len(r.data), hx(r.data, 24))
c0 = rf[b'CODE'][0].data
print('CODE0 hdr', hx(c0, 16))
for i in range(6):
    e = c0[16+i*8:16+i*8+8]; print('JT', i, hx(e, 8))
print('STRS', rf[b'STRS'][0].data[:60])
print('SIZE', rf[b'SIZE'][-1].data.hex())
# segment header: offset-of-first-JT-entry and count
for rid, r in sorted(rf[b'CODE'].items()):
    if rid: print('CODE', rid, struct.unpack('>HH', r.data[:4]), len(r.data))
