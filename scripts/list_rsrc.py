import sys
sys.path.insert(0, '/home/claude/castles-decomp/tools/rsrcfork/src')
import rsrcfork
rf = rsrcfork.open(sys.argv[1], fork='data')
for t, res in rf.items():
    ids = list(res.keys())
    tot = sum(len(r.data) for r in res.values())
    print(t.decode('mac_roman'), len(ids), tot, ids[:8])
