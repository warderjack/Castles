#!/bin/sh
# Run the CD release in the interpreter host (experimental). usage: cd/run.sh [MAXSLICES]
HERE=$(cd "$(dirname "$0")" && pwd)
NOLIFT=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy CASTLES_HEADLESS=1 MAXSLICES=${1:-300} timeout 300 "$HERE/build/host_cd" "${CDRSRC:-/tmp/cds/cdsample/Castles CD.rsrc}" "${CDGAME:-/tmp/cdgame}" 2>&1
