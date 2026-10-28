#!/bin/sh
# usage: lc.sh [script] [checks-per-routine] [timeout]  -> /tmp/claude-0/lc.log
cd "$(dirname "$0")"; LIFTTRACE=${LIFTTRACE} NORUN=1 ./run.sh 2>&1 </dev/null | grep -E "error|undefined"; rm -f /tmp/claude-0/lc.log
[ -n "$1" ] && export CASTLES_SCRIPT="$1"
( LIFTCHECK=${2:-3} CASTLES_HEADLESS=1 timeout ${3:-110} ./build/castles_host "../extract/Castles - Siege and Conquest" > /tmp/claude-0/lc.log 2>&1; echo finished >> /tmp/claude-0/lc.log ) </dev/null
