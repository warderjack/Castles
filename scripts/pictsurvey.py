import sys,struct,collections; sys.path.insert(0,'tools')
import rsrc
r=rsrc.parse_rsrc(open('work/castles/rsrc.fork','rb').read())
ops=collections.Counter()
for e in r:
    if e['type']!='PICT': continue
    d=e['data']; p=10; seen=[]
    def u16():
        global p; v=struct.unpack('>H',d[p:p+2])[0]; p+=2; return v
    while p<len(d):
        op=u16(); seen.append(op)
        if op==0x0011: p+=2; continue
        if op in (0x1E,0x0A): continue
        if op==0x00FF or op==0: break
        if op==0x0C00: p+=24
        elif op==0x0001: n=u16(); p+=n-2
        elif op in (0x0003,0x0004,0x0008,0x000D,0x0015,0x0016,0x00A0): p+=2
        elif op in (0x0007,0x0021,0x0022): p+= {7:4,0x21:4,0x22:6}[op] if op!=0x21 else 8
        elif op in (0x001A,0x001B,0x001C,0x001D,0x001F): p+=6
        elif op in (0x0002,0x0009,0x000A,0x0020,0x0030,0x0031,0x0032,0x0033,0x0034): p+=8
        elif op==0x00A1: p+=2; n=u16(); p+=n
        elif op in (0x0098,0x0099,0x009A,0x009B):
            if op>=0x9A: ops[('direct',e['id'])]+=1; break
            rb=u16(); 
            if not (rb&0x8000): ops['bitmap-old-in-98']+=1; break
            pm=d[p-2:p-2+46]; p+=44
            bounds=struct.unpack('>4h',pm[2:10]); packType=struct.unpack('>h',pm[10:12])[0]; px=struct.unpack('>h',pm[28:30])[0]
            seed,flags,size=struct.unpack('>IHh',d[p:p+8]); p+=8+8*(size+1)
            p+=16+2
            if op==0x99: n=u16(); p+=n-2
            rbm=rb&0x3FFF; h=bounds[2]-bounds[0]
            for y in range(h):
                if rbm>250: n=u16()
                else: n=d[p]; p+=1
                p+=n
            ops[(hex(op),'px',px,'pack',packType)]+=1
        elif op in (0x0090,0x0091): ops[('oldbits',e['id'])]+=1; break
        elif op in (0x8200,0x8201): ops[('QT',e['id'])]+=1; break
        elif 0x0100<=op<0x8000: p+=(op>>8)*2
        else: print('unknown',hex(op),e['id'],hex(p)); break
    print(e['id'],[hex(o) for o in seen[:14]])
print(ops)
