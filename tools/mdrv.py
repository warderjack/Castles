#!/usr/bin/env python3
"""Decode the MDRV music-driver resource: stream-cipher decrypt (game routine seg2+0xc22/0xc56), then LZSS (seg2+0x1df4)."""
import struct, sys, os
ROOT = os.path.join(os.path.dirname(__file__), '..')

def decrypt(b):
    b = bytearray(b); seed = 0xdce5
    for i in range(7, len(b)):
        c = b[i]
        b[i] = ((seed >> 8) ^ c) & 0xFF
        seed = ((((c + seed) & 0xFFFF) * 0xce6d) + 0x58bf) & 0xFFFF
    return bytes(b)

def lzss(src, outlen):
    out = bytearray(); i = 0
    while len(out) < outlen and i < len(src):
        flags = src[i]; i += 1
        for _ in range(8):
            if len(out) >= outlen: break
            if flags & 1:
                if i >= len(src): break
                out.append(src[i]); i += 1
            else:
                if i + 1 >= len(src): break
                d0 = (src[i] << 8) | src[i + 1]; i += 2
                off = d0 & 0xFFF; ln = (((d0 << 4 | d0 >> 12) & 0xF)) + 3   # rol.w #4: low nibble of rotated = top nibble
                # window reference: a3 - 0x1000 + off
                pos = len(out) - 0x1000 + off
                for _ in range(ln):
                    out.append(out[pos] if 0 <= pos < len(out) else 0); pos += 1
            flags >>= 1
    return bytes(out[:outlen])

def decode(path=None):
    d = open(path or os.path.join(ROOT, 'extracted/app/MDRV/11.bin'), 'rb').read()
    p = decrypt(d); n = struct.unpack('>I', p[:4])[0]
    return lzss(p[4:], n)

if __name__ == '__main__':
    d = open(os.path.join(ROOT, 'extracted/app/MDRV/11.bin'), 'rb').read()
    p = decrypt(d)
    print(p[:16].hex(), len(p))
