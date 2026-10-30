#!/usr/bin/env python3
"""Routine coverage of the translated game code.
Run scenarios with COVLOG=file (appends 'seg:offset' of every translated routine that ran), then:
  python3 scripts/coverage.py file [-v]     (-v lists routines never run)"""
import re, glob, collections, sys, os
os.chdir(os.path.join(os.path.dirname(__file__), '..', 'port'))
allr = collections.defaultdict(set)
for f in glob.glob('gen/lift_seg*.c'):
    for m in re.finditer(r'^void L_(\d+)_([0-9a-f]+)\(void\)', open(f).read(), re.M): allr[int(m[1])].add(int(m[2], 16))
cov = collections.defaultdict(set)
for l in open(sys.argv[1] if os.path.isabs(sys.argv[1]) else '../' + sys.argv[1]):
    z, o = l.strip().split(':'); cov[int(z)].add(int(o, 16))
c = t = 0
for z in sorted(allr):
    k = len(cov[z] & allr[z]); t += len(allr[z]); c += k
    print('seg %2d: %3d / %3d' % (z, k, len(allr[z])))
    if '-v' in sys.argv: print('   never run:', ' '.join('%x' % o for o in sorted(allr[z] - cov[z])))
print('total: %d / %d routines' % (c, t))
