import struct, sys, os
src, outdir = sys.argv[1], sys.argv[2]
os.makedirs(outdir, exist_ok=True)
d = open(src, 'rb').read()
name = d[2:2+d[1]].decode('mac_roman')
ftype, creator = d[65:69], d[69:73]
dlen = struct.unpack('>I', d[83:87])[0]
rlen = struct.unpack('>I', d[87:91])[0]
pad = lambda n: (n + 127) // 128 * 128
data = d[128:128+dlen]
rsrc = d[128+pad(dlen):128+pad(dlen)+rlen]
open(os.path.join(outdir, 'data.fork'), 'wb').write(data)
open(os.path.join(outdir, 'rsrc.fork'), 'wb').write(rsrc)
print(name, ftype, creator, 'data', dlen, 'rsrc', rlen)
