#!/bin/sh
# Optional speed-up for the CD release: translate its 68k code to C (from YOUR copy of the program) and build it into the host.
# usage: cd/lift_build.sh ["/path/to/Castles CD.rsrc"]     needs python3 and gcc; takes about a minute.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
R="${1:-$HOME/Castles-game/CD/Castles/Castles CD.rsrc}"
[ -f "$R" ] || { echo "Castles CD.rsrc not found: $R"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "python3 is needed for the translation step"; exit 1; }
L="$HERE/lifter/lift_cd.py"; [ -f "$L" ] || L="$HERE/../tools/lift_cd.py"
mkdir -p "$HERE/gen" "$HERE/build"
python3 "$L" gen "$R" "$HERE/gen"
( cd "$HERE/gen" && for f in lift_cd1 lift_cd_tab; do gcc -O1 -w -c -I../../port $f.c -o ../build/$f.o & done; wait )
[ -f "$HERE/build/lift_cd1.o" ] && [ -f "$HERE/build/lift_cd_tab.o" ] || { echo "compiling the translated code failed"; exit 1; }
"$HERE/build_host.sh"
