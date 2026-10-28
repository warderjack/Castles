import sys, struct
sys.path.insert(0, '/home/claude/castles-decomp/tools/capstone/bindings/python')
from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000, CS_MODE_BIG_ENDIAN
md = Cs(CS_ARCH_M68K, CS_MODE_M68K_000 | CS_MODE_BIG_ENDIAN)
seg = int(sys.argv[1]); start = int(sys.argv[2], 0); end = int(sys.argv[3], 0)
d = open(f'work/code/CODE_{seg}.bin','rb').read()[4:]
off = start
while off < end:
    ins = list(md.disasm(d[off:off+10], off, 1))
    if ins:
        i = ins[0]
        print(f'{i.address:05x}: {i.bytes.hex():<16} {i.mnemonic} {i.op_str}')
        off += i.size
    else:
        w = struct.unpack('>H', d[off:off+2])[0]
        print(f'{off:05x}: {w:04x}             dc.w ${w:04X}' + ('  ; ATRAP' if (w & 0xF000)==0xA000 else ''))
        off += 2
