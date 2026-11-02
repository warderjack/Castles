#!/usr/bin/env python3
"""disassemble CD CODE 1 file offsets: dis.py START END [segment=1]"""
import sys; sys.path.insert(0,'/home/claude/castles-decomp/tools')
import m68k
seg=sys.argv[3] if len(sys.argv)>3 else '1'
d=open(f'/home/claude/castles-decomp/cd/extracted/app/CODE/{seg}.bin','rb').read()
off=int(sys.argv[1],16); end=int(sys.argv[2],16)
while off<end:
    ins=m68k.decode(d,off,0)
    if not ins: print('%06x ??'%off); off+=2; continue
    print('%06x %s'%(off,ins.text())); off+=getattr(ins,'size',None) or ins.len
