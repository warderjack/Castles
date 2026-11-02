#!/bin/sh
# Start the CD release exactly like the manual command (no directory change, all output discarded).
HERE=$(cd "$(dirname "$0")/.." && pwd); G="${1:-$HOME/Castles-game/CD/Castles}"
NEW=0; [ -f "$G/Castles CD Prefs.bin" ] || NEW=1
[ -f "$HERE/cd/build/lift_cd1.o" ] || export NOLIFT=1
"$HERE/cd/build/host_cd" "$G/Castles CD.rsrc" "$G" >/dev/null 2>&1
[ "$NEW" = 1 ] && [ -f "$G/Castles CD Prefs.bin" ] && "$HERE/cd/build/host_cd" "$G/Castles CD.rsrc" "$G" >/dev/null 2>&1
exit 0
