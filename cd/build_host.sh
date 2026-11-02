#!/bin/sh
# Experimental: build the interpreter-based host for the CD release into cd/build/ (does not touch port/build/ outputs).
# Reads the shared port sources and the already-built floppy lift objects (unused at run time: run with NOLIFT=1).
set -e
mkdir -p "$(dirname "$0")/build"
HERE=$(cd "$(dirname "$0")" && pwd); P="$HERE/../port"; cd "$P"
SDLC=""; SDLL=""
if command -v sdl2-config >/dev/null 2>&1; then SDLC="-DHAVE_SDL $(sdl2-config --cflags)"; SDLL="$(sdl2-config --libs)"
elif [ -x ../tools/sdl2-install/bin/sdl2-config ]; then Q=$(cd ../tools/sdl2-install && pwd); SDLC="-DHAVE_SDL -I$Q/include/SDL2 -D_REENTRANT"; SDLL="-L$Q/lib -Wl,-rpath,$Q/lib -lSDL2"; fi
gcc ${CDDEBUG:--O2} -w $SDLC -DMUSASHI_CNF='"castles_conf.h"' -I. -Imusashi-upstream -Ibuild -o "$HERE/build/host_cd" \
    host.c build/lift_*.o musashi-upstream/m68kcpu.c build/m68kops.c musashi-upstream/softfloat/softfloat.c $SDLL -lm
echo "built cd/build/host_cd"
