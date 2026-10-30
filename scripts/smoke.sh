#!/bin/bash
# Smoke test: run every scripted play-through headless and fail on unlifted code, unimplemented traps or crashes.
# usage: scripts/smoke.sh [binary]   (default port/build/castles_native)
cd "$(dirname "$0")/.."
BIN=${1:-port/build/castles_native}; mkdir -p /tmp/castles-play; rc=0
for s in scripts/play/*.txt; do
  [ "$(basename $s)" = long_monkey.txt -o "$(basename $s)" = long_soak.txt ] && [ -z "$LONG" ] && continue
  n=$(basename $s .txt)
  CASTLES_HEADLESS=1 CASTLES_SCRIPT=$s timeout 400 $BIN work/castles/rsrc.fork "extract/Castles - Siege and Conquest" > /tmp/castles-play/$n.log 2>&1; e=$?
  bad=$(grep -a -c "\[native\]\|unimplemented trap\|did not return\|Segmentation" /tmp/castles-play/$n.log)
  if [ "$bad" != 0 ] || [ $e -gt 3 ]; then echo "FAIL $n (exit $e, $bad problems)"; rc=1; else echo "ok   $n"; fi
done
exit $rc
