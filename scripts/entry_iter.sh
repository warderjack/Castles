#!/bin/bash
# one round of the feedback loop: run a scenario with ENTLOG, merge observed unlifted entry points, regenerate and rebuild
cd /home/claude/castles-decomp
SC=${1:-/tmp/claude-0/c1.txt}
rm -f /tmp/claude-0/ent_it.txt
(cd port && ENTLOG=/tmp/claude-0/ent_it.txt CASTLES_HEADLESS=1 CASTLES_SCRIPT=$SC timeout 100 ./build/castles_host ../work/castles/rsrc.fork "../extract/Castles - Siege and Conquest" >/dev/null 2>&1)
before=$(wc -l < gen/entries_extra.txt)
cat /tmp/claude-0/ent_it.txt gen/entries_extra.txt | sort -u > /tmp/claude-0/ent_m.txt && cp /tmp/claude-0/ent_m.txt gen/entries_extra.txt
echo "entries: $before -> $(wc -l < gen/entries_extra.txt)"
python3 tools/lift.py 2>&1 | tail -1
(cd port && NORUN=1 ./run.sh 2>&1 | grep -i " error")
