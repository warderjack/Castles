#!/bin/sh
# build + run + show tail of the log
cd "$(dirname "$0")"
gcc -O2 -w -DMUSASHI_CNF='"castles_conf.h"' -I. -Imusashi-upstream -Ibuild -o build/castles_host host.c musashi-upstream/m68kcpu.c build/m68kops.c musashi-upstream/softfloat/softfloat.c -lm 2>&1 | head -30
(timeout ${T:-60} ./build/castles_host ../work/castles/rsrc.fork "../extract/Castles - Siege and Conquest" > build/run.log 2>&1; true)
grep -c "^\[trap" build/run.log | sed 's/^/traps logged: /'
tail -${N:-5} build/run.log | cut -c1-260
