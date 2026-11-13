#!/usr/bin/env python3
"""Minimal MacBinary + Mac resource fork reader/extractor (no dependencies)."""
import struct, sys, os, json

def read_macbinary(path):
    b = open(path, 'rb').read()
    assert b[0] == 0 and b[74] == 0 and b[82] == 0, "not MacBinary"
    namelen = b[1]
    name = b[2:2+namelen].decode('mac_roman')
    ftype, fcreator = b[65:69], b[69:73]
    dlen, rlen = struct.unpack('>II', b[83:91])
    pos = 128
    data = b[pos:pos+dlen]
    pos += (dlen + 127) // 128 * 128
    rsrc = b[pos:pos+rlen]
    return dict(name=name, type=ftype, creator=fcreator, data=data, rsrc=rsrc)

def parse_rsrc(r):
    doff, moff, dlen, mlen = struct.unpack('>IIII', r[:16])
    m = r[moff:moff+mlen]
    tl_off, nl_off = struct.unpack('>HH', m[24:28])
    ntypes = struct.unpack('>H', m[tl_off:tl_off+2])[0] + 1
    out = []
    for i in range(ntypes):
        t, cnt, ro = struct.unpack('>4sHH', m[tl_off+2+i*8: tl_off+2+i*8+8])
        cnt += 1
        for j in range(cnt):
            e = m[tl_off+ro+j*12: tl_off+ro+j*12+12]
            rid, noff, ad = struct.unpack('>hHI', e[:8])
            attrs, rdoff = ad >> 24, ad & 0xFFFFFF
            name = None
            if noff != 0xFFFF:
                n = nl_off + noff
                name = m[n+1:n+1+m[n]].decode('mac_roman')
            ln = struct.unpack('>I', r[doff+rdoff:doff+rdoff+4])[0]
            data = r[doff+rdoff+4:doff+rdoff+4+ln]
            out.append(dict(type=t.decode('mac_roman'), id=rid, name=name, attrs=attrs, data=data))
    return out

if __name__ == '__main__':
    src, outdir = sys.argv[1], sys.argv[2]
    mb = read_macbinary(src)
    res = parse_rsrc(mb['rsrc'])
    os.makedirs(outdir, exist_ok=True)
    index = []
    for r in res:
        d = os.path.join(outdir, r['type'].replace('/', '_').replace(' ', '_'))
        os.makedirs(d, exist_ok=True)
        fn = os.path.join(d, f"{r['id']}.bin")
        open(fn, 'wb').write(r['data'])
        index.append(dict(type=r['type'], id=r['id'], name=r['name'], attrs=r['attrs'], size=len(r['data'])))
    json.dump(index, open(os.path.join(outdir, 'index.json'), 'w'), indent=1)
    from collections import Counter
    c = Counter(r['type'] for r in res)
    print(f"{mb['name']!r} type={mb['type']} creator={mb['creator']} data={len(mb['data'])} rsrc={len(mb['rsrc'])}")
    for t, n in sorted(c.items()):
        tot = sum(x['size'] for x in index if x['type'] == t)
        print(f"  {t!r:8} x{n:<4} {tot:>9} bytes")
